#pragma once

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <malloc.h>
#include <math.h>
#include <switch.h>
#include <arpa/inet.h>

#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef u32 Result;

#include <inttypes.h>


#ifdef _WIN32
#define DIRECTORY_SEPARATOR_CHAR '\\'
static const char DIRECTORY_SEPARATOR[] = "\\";
#else
#define DIRECTORY_SEPARATOR_CHAR '/'
static const char DIRECTORY_SEPARATOR[] = "/";
#endif

#define M_TAU (2*M_PI)

typedef union {
    uint32_t abgr;
    struct {
        uint8_t r,g,b,a;
    };
} color_t;

#define ENTRY_NAMELENGTH   0x200
#define ENTRY_AUTHORLENGTH 0x100
#define ENTRY_VERLENGTH   0x10
#define ENTRY_ARGBUFSIZE 0x400

//#define HARD_DBG(...) { char buffer[5000]; sprintf(buffer,__VA_ARGS__); FILE * fp=fopen("/nxlink.txt","a"); fprintf(fp,__VA_ARGS__); fclose(fp);}



typedef struct
{
    char* dst;
    uint32_t buf[ENTRY_ARGBUFSIZE/sizeof(uint32_t)];
    struct in_addr nxlink_host;
} argData_s;

struct fileManager
{
    char path[(2048)+8];
    argData_s args;

    char name[ENTRY_NAMELENGTH+1];
    char author[ENTRY_AUTHORLENGTH+1];
    char version[ENTRY_VERLENGTH+1];

    uint8_t *icon;
    size_t icon_size;
    uint8_t *icon_gfx;
    uint8_t *icon_gfx_small;

    NacpStruct *nacp;

};

typedef struct fileManager fileHolder;

fileHolder loadedFile;



char* filePath;

typedef struct
{
    // Mandatory fields
    const char* name;
    //u32 flags;
    bool (* init)(void);
    void (* deinit)(void);
    //void (* launchFile)(const char* path, argData_s* args);

    // Optional fields
    //void (* useTitle)(u64 tid, u8 mediatype);
} loaderFuncs_s;

void launchInit(void);
void launchExit(void);
//const loaderFuncs_s* launchGetLoader(void);
size_t launchAddArg(argData_s* ad, const char* arg);
void launchAddArgsFromString(argData_s* ad, char* arg);
int launchFile(const char* path, argData_s* args);
void printArgs(const char* path, argData_s* args);