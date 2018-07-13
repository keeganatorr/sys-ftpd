#define ENABLE_LOGGING 1
/* This FTP server implementation is based on RFC 959,
 * (https://tools.ietf.org/html/rfc959), RFC 3659
 * (https://tools.ietf.org/html/rfc3659) and suggested implementation details
 * from https://cr.yp.to/ftp/filesystem.html
 */
#include "netloader.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <malloc.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>
#include <sys/types.h>
#ifdef _3DS
#include <3ds.h>
#define lstat stat
#elif defined(__SWITCH__)
#include <switch.h>
#define lstat stat
#else
#include <stdbool.h>
#define BIT(x) (1 << (x))
#endif
#include "console.h"
#include "util.h"
#include "common.h"


#define POLL_UNKNOWN (~(POLLIN | POLLPRI | POLLOUT))

#define XFER_BUFFERSIZE 4096 //8192
#define SOCK_BUFFERSIZE 4096
#define FILE_BUFFERSIZE 16384
#define CMD_BUFFERSIZE 4096

#define LISTEN_PORT 5000
#ifdef _3DS
#define DATA_PORT (LISTEN_PORT + 1)
#else
#define DATA_PORT 0 /* ephemeral port */
#endif

#define ZLIB_CHUNK (16 * 1024)
//#define FILE_BUFFER_SIZE (128*1024)
unsigned char in[ZLIB_CHUNK];
unsigned char out[ZLIB_CHUNK];



typedef struct ftp_session_t ftp_session_t;


/*! session state */
typedef enum
{
  COMMAND_STATE,       /*!< waiting for a command */
  DATA_CONNECT_STATE,  /*!< waiting for connection after PASV command */
  DATA_TRANSFER_STATE, /*!< data transfer in progress */
} session_state_t;

/*! ftp_session_set_state flags */
typedef enum
{
  CLOSE_PASV = BIT(0), /*!< Close the pasv_fd */
  CLOSE_DATA = BIT(1), /*!< Close the data_fd */
} set_state_flags_t;

/*! ftp_session_t flags */
typedef enum
{
  SESSION_BINARY = BIT(0), /*!< data transfers in binary mode */
  SESSION_PASV = BIT(1),   /*!< have pasv_addr ready for data transfer command */
  SESSION_PORT = BIT(2),   /*!< have peer_addr ready for data transfer command */
  SESSION_RECV = BIT(3),   /*!< data transfer in source mode */
  SESSION_SEND = BIT(4),   /*!< data transfer in sink mode */
  SESSION_RENAME = BIT(5), /*!< last command was RNFR and buffer contains path */
  SESSION_URGENT = BIT(6), /*!< in telnet urgent mode */
} session_flags_t;

/*! ftp_xfer_dir mode */
typedef enum
{
  XFER_DIR_LIST, /*!< Long list */
  XFER_DIR_MLSD, /*!< Machine list directory */
  XFER_DIR_MLST, /*!< Machine list */
  XFER_DIR_NLST, /*!< Short list */
  XFER_DIR_STAT, /*!< Stat command */
} xfer_dir_mode_t;

typedef enum
{
  SESSION_MLST_TYPE = BIT(0),
  SESSION_MLST_SIZE = BIT(1),
  SESSION_MLST_MODIFY = BIT(2),
  SESSION_MLST_PERM = BIT(3),
  SESSION_MLST_UNIX_MODE = BIT(4),
} session_mlst_flags_t;

/*! ftp session */
struct ftp_session_t
{
  char cwd[4096];                  /*!< current working directory */
  char lwd[4096];                  /*!< list working directory */
  struct sockaddr_in peer_addr;    /*!< peer address for data connection */
  struct sockaddr_in pasv_addr;    /*!< listen address for PASV connection */
  int cmd_fd;                      /*!< socket for command connection */
  int pasv_fd;                     /*!< listen socket for PASV */
  int data_fd;                     /*!< socket for data transfer */
  time_t timestamp;                /*!< time from last command */
  session_flags_t flags;           /*!< session flags */
  xfer_dir_mode_t dir_mode;        /*!< dir transfer mode */
  session_mlst_flags_t mlst_flags; /*!< session MLST flags */
  session_state_t state;           /*!< session state */
  ftp_session_t *next;             /*!< link to next session */
  ftp_session_t *prev;             /*!< link to prev session */

  loop_status_t (*transfer)(ftp_session_t *); /*! data transfer callback */
  char buffer[XFER_BUFFERSIZE];               /*! persistent data between callbacks */
  char file_buffer[FILE_BUFFERSIZE];          /*! stdio file buffer */
  char cmd_buffer[CMD_BUFFERSIZE];            /*! command buffer */
  size_t bufferpos;                           /*! persistent buffer position between callbacks */
  size_t buffersize;                          /*! persistent buffer size between callbacks */
  size_t cmd_buffersize;
  uint64_t filepos;  /*! persistent file position between callbacks */
  uint64_t filesize; /*! persistent file size between callbacks */
  FILE *fp;          /*! persistent open file pointer between callbacks */
  DIR *dp;           /*! persistent open directory pointer between callbacks */
};


static void update_free_space(void);

#ifdef _3DS
/*! SOC service buffer */
static u32 *SOCU_buffer = NULL;

/*! Whether LCD is powered */
static bool lcd_power = true;

/*! aptHook cookie */
static aptHookCookie cookie;
#elif defined(__SWITCH__)

/*! appletHook cookie */
static AppletHookCookie cookie;
#endif

/*! server listen address */
static struct sockaddr_in serv_addr;
/*! listen file descriptor */
static int listenfd = -1;
#ifdef _3DS
/*! current data port */
static in_port_t data_port = DATA_PORT;
#endif

/*! server start time */
static time_t start_time = 0;



//__attribute__((format(printf, 3, 4)))

//---------------------------------------------------------------------------------
static int recvall(int sock, void *buffer, int size, int flags) {
//---------------------------------------------------------------------------------
    int len, sizeleft = size;

    while (sizeleft) {

        len = recv(sock,buffer,sizeleft,flags);

        if (len == 0) {
            size = 0;
            break;
        };

        if (len != -1) {
            sizeleft -=len;
            buffer +=len;
        } else {
#ifdef _WIN32
            int errcode = WSAGetLastError();
            if (errcode != WSAEWOULDBLOCK) {
                netloader_error("win socket error",errcode);
                break;
            }
#else
            if ( errno != EWOULDBLOCK && errno != EAGAIN) {
                perror(NULL);
                break;
            }
#endif
        }
    }
    return size;
}


//---------------------------------------------------------------------------------
static int decompress(int sock, FILE *fh, size_t filesize) {
//---------------------------------------------------------------------------------
    int ret;
    unsigned have;
    z_stream strm;
    size_t chunksize;

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK) {
        printf("inflateInit failed. %d\n",ret);
        return ret;
    }

    size_t total = 0;
    /* decompress until deflate stream ends or end of file */
    do {

        int len = recvall(sock, &chunksize, 4, 0);

        if (len != 4) {
            (void)inflateEnd(&strm);
            printf("Error getting chunk size %d\n",len);
            return Z_DATA_ERROR;
        }

    strm.avail_in = recvall(sock,in,chunksize,0);

    if (strm.avail_in == 0) {
        (void)inflateEnd(&strm);
        printf("remote closed socket. %d\n",0);
        return Z_DATA_ERROR;
    }

    strm.next_in = in;

    /* run inflate() on input until output buffer not full */
    do {
        strm.avail_out = ZLIB_CHUNK;
        strm.next_out = out;
        ret = inflate(&strm, Z_NO_FLUSH);

        switch (ret) {

            case Z_NEED_DICT:
            ret = Z_DATA_ERROR;     /* and fall through */

            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
            case Z_STREAM_ERROR:
                (void)inflateEnd(&strm);
                printf("inflate error %d\n",ret);
                return ret;
        }

        have = ZLIB_CHUNK - strm.avail_out;

        if (fwrite(out, 1, have, fh) != have || ferror(fh)) {
            (void)inflateEnd(&strm);
            printf("file write error %d\n",0);
            return Z_ERRNO;
        }

        total += have;
        //printf("%zu (%zd%%)",total, (100 * total) / filesize);
    } while (strm.avail_out == 0);

    /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}



//---------------------------------------------------------------------------------
int loadnro(fileHolder *filePointerInner, int sock, struct in_addr remote) {
//---------------------------------------------------------------------------------
    int len, namelen, filelen;
    char filename[PATH_MAX+1];
    len = recvall(sock, &namelen, 4, 0);

    if (len != 4) {
        printf("Error getting name length %d\n", errno);
        return -1;
    }

    if (namelen >= sizeof(filename)-1) {
        printf("Filename length is too large %d\n",errno);
        return -1;
    }

    len = recvall(sock, filename, namelen, 0);

    if (len != namelen) {
        printf("Error getting filename %d\n", errno);
        return -1;
    }

    filename[namelen] = 0;

    len = recvall(sock, &filelen, 4, 0);

    if (len != 4) {
        printf("Error getting file length %d\n",errno);
        return -1;
    }

    printf("Filename: %s\n",filename);

    //me->path = malloc(1024*sizeof(char));

    // WORKS UP TO HERE SO FAR
    
    int response = 0;

    //sanitisePath(filename);
    sprintf(filePointerInner->path,"sdmc:/switch/%s",filename);
    //snprintf(me->path, sizeof(me->path)-1, "%s%s", "sdmc:/switch/", filename);
    //me->path[PATH_MAX] = 0;
    // make sure it's terminated
    //me->path[PATH_MAX] = 0;

    printf("Filepath: %s\n",filePointerInner->path);

    //free(filePointerInner->path);
    argData_s* ad = &filePointerInner->args;
    ad->dst = (char*)&ad->buf[1];
    ad->nxlink_host = remote;

    launchAddArg(ad, filePointerInner->path);

    int fd = open(filePointerInner->path,O_CREAT|O_WRONLY, ACCESSPERMS);

    if (fd < 0) {
        response = -1;
        printf("open %d\n", errno);
    } else {
        if (ftruncate(fd,filelen) == -1) {
            response = -2;
            printf("ftruncate %d\n",errno);
        }
        close(fd);
    }

    FILE *file = NULL;

    if (response == 0) file = fopen(filePointerInner->path,"wb");

    if(NULL == file) {
        perror("file");
        response = -1;
    }

    send(sock,(char *)&response,sizeof(response),0);

    if (response == 0 ) {

        //char *writebuffer=malloc(FILE_BUFFER_SIZE);
        //setvbuf(file,writebuffer,_IOFBF, FILE_BUFFER_SIZE);

        printf("transferring %s\n%d bytes.\n", filename, filelen);

        if (decompress(sock,file,filelen)==Z_OK) {
            int netloaded_cmdlen = 0;
            send(sock,(char *)&response,sizeof(response),0);
            //printf("\ntransferring command line\n");
            len = recvall(sock,(char*)&netloaded_cmdlen,4,0);

            len = recvall(sock,filePointerInner->args.dst, netloaded_cmdlen,0);

            while(netloaded_cmdlen) {
                size_t len = strlen(filePointerInner->args.dst) + 1;
                ad->dst += len;
                ad->buf[0]++;
                netloaded_cmdlen -= len;
            }

        } else {
            response = -1;
        }

        //free(writebuffer);
        fflush(file);
        fclose(file);

    }

    return response;
    
   //return 0;
}


/*! allocate new ftp session
 *
 *  @param[in] listen_fd socket to accept connection from
 */
static int
netloader_session_loop(int listen_fd, fileHolder *filePointer)
{
  ssize_t rc;
  int new_fd;
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);

  /* accept connection */
  new_fd = accept(listen_fd, (struct sockaddr *)&addr, &addrlen);
  if (new_fd < 0)
  {
    console_print(RED "accept: %d %s\n" RESET, errno, strerror(errno));
    return -1;
  }

  console_print(CYAN "accepted connection from %s:%u\n" RESET,
                inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

  if(new_fd >= 0)
    {
        printf("loadfile: %d\n", new_fd);
        int result = loadnro(filePointer, new_fd,addr.sin_addr);
        //netloader_deactivate();
        if (result== 0) {
            printf("Loadeda!\n");
            printArgs(filePointer->path, &filePointer->args);
            return 1;
        } else {
            return -1;
        }
    }

  return 0;
}




/* Update free space in status bar */
static void
update_free_space(void)
{
#if defined(_3DS) || defined(__SWITCH__)
#define KiB (1024.0)
#define MiB (1024.0 * KiB)
#define GiB (1024.0 * MiB)
  char buffer[16];
  struct statvfs st;
  double bytes_free;
  int rc, len;

  rc = statvfs("sdmc:/", &st);
  if (rc != 0)
    console_print(RED "statvfs: %d %s\n" RESET, errno, strerror(errno));
  else
  {
    bytes_free = (double)st.f_bsize * st.f_bfree;

    if (bytes_free < 1000.0)
      len = snprintf(buffer, sizeof(buffer), "%.0lfB", bytes_free);
    else if (bytes_free < 10.0 * KiB)
      len = snprintf(buffer, sizeof(buffer), "%.2lfKiB", floor((bytes_free * 100.0) / KiB) / 100.0);
    else if (bytes_free < 100.0 * KiB)
      len = snprintf(buffer, sizeof(buffer), "%.1lfKiB", floor((bytes_free * 10.0) / KiB) / 10.0);
    else if (bytes_free < 1000.0 * KiB)
      len = snprintf(buffer, sizeof(buffer), "%.0lfKiB", floor(bytes_free / KiB));
    else if (bytes_free < 10.0 * MiB)
      len = snprintf(buffer, sizeof(buffer), "%.2lfMiB", floor((bytes_free * 100.0) / MiB) / 100.0);
    else if (bytes_free < 100.0 * MiB)
      len = snprintf(buffer, sizeof(buffer), "%.1lfMiB", floor((bytes_free * 10.0) / MiB) / 10.0);
    else if (bytes_free < 1000.0 * MiB)
      len = snprintf(buffer, sizeof(buffer), "%.0lfMiB", floor(bytes_free / MiB));
    else if (bytes_free < 10.0 * GiB)
      len = snprintf(buffer, sizeof(buffer), "%.2lfGiB", floor((bytes_free * 100.0) / GiB) / 100.0);
    else if (bytes_free < 100.0 * GiB)
      len = snprintf(buffer, sizeof(buffer), "%.1lfGiB", floor((bytes_free * 10.0) / GiB) / 10.0);
    else
      len = snprintf(buffer, sizeof(buffer), "%.0lfGiB", floor(bytes_free / GiB));

    console_set_status("\x1b[0;%dH" GREEN "%s", 50 - len, buffer);
  }
#endif
}

/*! Update status bar */
static int
update_status(void)
{
#if defined(_3DS) || defined(__SWITCH__)
//  console_set_status("\n" GREEN STATUS_STRING " "
#ifdef ENABLE_LOGGING
//                     "DEBUG "
#endif
  //                    CYAN "%s:%u" RESET,
  //                  inet_ntoa(serv_addr.sin_addr),
  //                ntohs(serv_addr.sin_port));
  update_free_space();
#elif 0 //defined(__SWITCH__)
  char hostname[128];
  socklen_t addrlen = sizeof(serv_addr);
  int rc;
  rc = gethostname(hostname, sizeof(hostname));
  if (rc != 0)
  {
    console_print(RED "gethostname: %d %s\n" RESET, errno, strerror(errno));
    return -1;
  }
  console_set_status("\n" GREEN STATUS_STRING " test "
#ifdef ENABLE_LOGGING
                     "DEBUG "
#endif
                     CYAN "%s:%u" RESET,
                     hostname,
                     ntohs(serv_addr.sin_port));
  update_free_space();
#else
  char hostname[128];
  socklen_t addrlen = sizeof(serv_addr);
  int rc;

  rc = getsockname(listenfd, (struct sockaddr *)&serv_addr, &addrlen);
  if (rc != 0)
  {
    console_print(RED "getsockname: %d %s\n" RESET, errno, strerror(errno));
    return -1;
  }

  rc = gethostname(hostname, sizeof(hostname));
  if (rc != 0)
  {
    console_print(RED "gethostname: %d %s\n" RESET, errno, strerror(errno));
    return -1;
  }

  console_set_status(GREEN STATUS_STRING " "
#ifdef ENABLE_LOGGING
                                         "DEBUG "
#endif
                     YELLOW "IP:" CYAN "%s " YELLOW "Port:" CYAN "%u" RESET,
                     hostname,
                     ntohs(serv_addr.sin_port));
#endif

  return 0;
}

#ifdef _3DS
/*! Handle apt events
 *
 *  @param[in] type    Event type
 *  @param[in] closure Callback closure
 */
static void
apt_hook(APT_HookType type,
         void *closure)
{
  switch (type)
  {
  case APTHOOK_ONSUSPEND:
  case APTHOOK_ONSLEEP:
    /* turn on backlight, or you can't see the home menu! */
    if (R_SUCCEEDED(gspLcdInit()))
    {
      GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTH);
      gspLcdExit();
    }
    break;

  case APTHOOK_ONRESTORE:
  case APTHOOK_ONWAKEUP:
    /* restore backlight power state */
    if (R_SUCCEEDED(gspLcdInit()))
    {
      (lcd_power ? GSPLCD_PowerOnBacklight : GSPLCD_PowerOffBacklight)(GSPLCD_SCREEN_BOTH);
      gspLcdExit();
    }
    break;

  default:
    break;
  }
}
#elif defined(__SWITCH__)
/*! Handle applet events
 *
 *  @param[in] type    Event type
 *  @param[in] closure Callback closure
 */
static void
applet_hook(AppletHookType type,
            void *closure)
{
  (void)closure;
  (void)type;
  /* stubbed for now */
  switch (type)
  {
  default:
    break;
  }
}
#endif

/*! initialize ftp subsystem */
int netloader_activate(void)
{
  int rc;

  start_time = time(NULL);

  static const SocketInitConfig socketInitConfig = {
      .bsdsockets_version = 1,

      .tcp_tx_buf_size = 8 * SOCK_BUFFERSIZE,
      .tcp_rx_buf_size = 8 * SOCK_BUFFERSIZE,
      .tcp_tx_buf_max_size = 16 * SOCK_BUFFERSIZE,
      .tcp_rx_buf_max_size = 16 * SOCK_BUFFERSIZE,

      .udp_tx_buf_size = 0x2400,
      .udp_rx_buf_size = 0xA500,

      .sb_efficiency = 8,
  };

  Result ret = socketInitialize(&socketInitConfig);
  if (ret != 0)
  {
    fatalLater(ret);
  }

  /* register applet hook */
  appletHook(&cookie, applet_hook, NULL);

  /* allocate socket to listen for clients */
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0)
  {
    console_print(RED "socket: %d %s\n" RESET, errno, strerror(errno));
    netloader_deactivate();
    return -1;
  }

  /* get address to listen on */
  serv_addr.sin_family = AF_INET;
#ifdef _3DS
  serv_addr.sin_addr.s_addr = gethostid();
  serv_addr.sin_port = htons(LISTEN_PORT);
#else
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(NXLINK_SERVER_PORT);
#endif

  /* reuse address */
  {
    int yes = 1;
    rc = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (rc != 0)
    {
      console_print(RED "setsockopt: %d %s\n" RESET, errno, strerror(errno));
      netloader_deactivate();
      return -1;
    }
  }

  /* bind socket to listen address */
  rc = bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  if (rc != 0)
  {
    console_print(RED "bind: %d %s\n" RESET, errno, strerror(errno));
    netloader_deactivate();
    return -1;
  }

  /* listen on socket */
  rc = listen(listenfd, 5);
  if (rc != 0)
  {
    console_print(RED "listen: %d %s\n" RESET, errno, strerror(errno));
    netloader_deactivate();
    return -1;
  }

  /* print server address */
  rc = update_status();
  if (rc != 0)
  {
    netloader_deactivate();
    return -1;
  }

  return 0;

#ifdef _3DS
soc_fail:
  free(SOCU_buffer);
  SOCU_buffer = NULL;

memalign_fail:
  return -1;
#endif
}

/*! deinitialize ftp subsystem */
void netloader_deactivate(void)
{
#if defined(_3DS)
  Result ret;
#endif

  debug_print("exiting ftp server\n");

  /* stop listening for new clients */
  if (listenfd >= 0)
    //ftp_closesocket(listenfd, false);

#ifdef _3DS
  /* deinitialize SOC service */
  console_render();
  console_print(CYAN "Waiting for socExit()...\n" RESET);

  if (SOCU_buffer != NULL)
  {
    ret = socExit();
    if (ret != 0)
      console_print(RED "socExit: 0x%08X\n" RESET, (unsigned int)ret);
    free(SOCU_buffer);
  }
#elif defined(__SWITCH__)
  /* deinitialize socket driver */
  console_render();
  console_print(CYAN "Waiting for socketExit()...\n" RESET);

  socketExit();

#endif
}

/*! ftp look
 *
 *  @returns whether to keep looping
 */
loop_status_t
netloader_loop(void)
{
  int rc;
  struct pollfd pollinfo;

  /* we will poll for new client connections */
  pollinfo.fd = listenfd;
  pollinfo.events = POLLIN;
  pollinfo.revents = 0;

  /* poll for a new client */
  /* if < 0 we error */
  /* if = 0 we exit and try again */
  /* if > 0 we have a new client */
  rc = poll(&pollinfo, 1, 0);
  if (rc < 0)
  {
    /* wifi got disabled */
    console_print(RED "poll: FAILED!\n" RESET);

    if (errno == ENETDOWN)
      return LOOP_RESTART;

    console_print(RED "poll: %d %s\n" RESET, errno, strerror(errno));
    return LOOP_EXIT;
  }
  else if (rc > 0)
  {
    if (pollinfo.revents & POLLIN)
    {
      /* we got a new client */
      int newClient = netloader_session_loop(listenfd, &loadedFile);
      if ((newClient != 0) && (newClient != 1))
      {
        return LOOP_RESTART;
      }
      else if (newClient == 1)
      {
        printf("Exit Loop Good!\n");
        return LOOP_EXIT;
      }
    }
    else
    {
      console_print(YELLOW "listenfd: revents=0x%08X\n" RESET, pollinfo.revents);
    }
  }

  return LOOP_CONTINUE;
}
