#include "common.h"

static char argBuf[ENTRY_ARGBUFSIZE];

size_t launchAddArg(argData_s* ad, const char* arg) {
    size_t len = strlen(arg)+1;
    if ((ad->dst+len) >= (char*)(ad+1)) return len; // Overflow
    ad->buf[0]++;
    strcpy(ad->dst, arg);
    ad->dst += len;
    return len;
}

void launchAddArgsFromString(argData_s* ad, char* arg) {
    char c, *pstr, *str=arg, *endarg = arg+strlen(arg);

    do
    {
        do
        {
            c = *str++;
        } while ((c == ' ' || c == '\t') && str < endarg);

        pstr = str-1;

        if (c == '\"')
        {
            pstr++;
            while(*str++ != '\"' && str < endarg);
        }
        else if (c == '\'')
        {
            pstr++;
            while(*str++ != '\'' && str < endarg);
        }
        else
        {
            do
            {
                c = *str++;
            } while (c != ' ' && c != '\t' && str < endarg);
        }

        str--;

        if (str == (endarg - 1))
        {
            if(*str == '\"' || *str == '\'')
                *(str++) = 0;
            else
                str++;
        }
        else
        {
                *(str++) = '\0';
        }

        launchAddArg(ad, pstr);

    } while(str<endarg);
}


static char *init_args(char *dst, size_t dst_maxsize, u32 *in_args, size_t size)
{
    size_t tmplen;
    u32 argi;
    char *in_argdata = (char*)&in_args[1];

    size-= sizeof(u32);

    for (argi=0; argi<in_args[0]; argi++) {
        if (size < 2) break;

        tmplen = strnlen(in_argdata, size-1);

        if (tmplen+3 > dst_maxsize) break;

        if (dst_maxsize < 3) break;

        *dst++ = '"';
        dst_maxsize--;

        strncpy(dst, in_argdata, tmplen);
        in_argdata+= tmplen+1;
        size-= tmplen+1;
        dst+= tmplen;
        dst_maxsize-= tmplen;

        *dst++ = '"';
        dst_maxsize--;

        if (argi+1 < in_args[0]) {
            *dst++ = ' ';
            dst_maxsize--;
        }
    }
    return dst;
}

static bool init(void)
{
    return envHasNextLoad();
}

static void deinit(void)
{

}

int launchFile(const char* path, argData_s* args)
{
    /*if (strncmp(path, "sdmc:/",6) == 0)
        path += 5;*/
    memset(argBuf, 0, sizeof(argBuf));

   uint32_t remote = args->nxlink_host.s_addr;

   if (remote) {
        char nxlinked[17];
        sprintf(nxlinked,"%08" PRIx32 "_NXLINK_",remote);
        launchAddArg(args, nxlinked);
    }

    init_args(argBuf, sizeof(argBuf)-1, args->buf, sizeof(args->buf));

    Result rc = envSetNextLoad(path, argBuf);

    if(R_FAILED(rc)) fatalSimple(rc);//TODO: How should failing be handled?
    return 1;
    //uiExitLoop();
}

void printArgs(const char* path, argData_s* args)
{
    /*if (strncmp(path, "sdmc:/",6) == 0)
        path += 5;*/
    memset(argBuf, 0, sizeof(argBuf));

   uint32_t remote = args->nxlink_host.s_addr;
    char nxlinked[17];
   if (remote) {
        
        sprintf(nxlinked,"%08" PRIx32 "_NXLINK_",remote);
        launchAddArg(args, nxlinked);
        printf("nxlinked: %s\n",nxlinked);
    }

    init_args(argBuf, sizeof(argBuf)-1, args->buf, sizeof(args->buf));
    printf("path: %s argbuf: %s\n",path,argBuf);

    FILE *textFile = NULL;
    textFile = fopen("sdmc:/.nxlink","w");
    fputs(path,textFile);
    fputs("\n",textFile);
    fputs(nxlinked,textFile);
    fclose(textFile);

    //Result rc = envSetNextLoad(path, argBuf);

    /*if(R_FAILED(rc)) fatalSimple(rc);//TODO: How should failing be handled?
    return 1;*/
    //uiExitLoop();
}

const loaderFuncs_s loader_builtin =
{
    .name = "builtin",
    .init = init,
    .deinit = deinit,
    //.launchFile = launchFile,
};