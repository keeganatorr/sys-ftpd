#include <string.h>
#include <stdio.h>
#include <dirent.h>

#include <errno.h>
#include <malloc.h>
#include <stdarg.h>
#include <unistd.h>
#include "console.h"
#include "netloader.h"
#include "common.h"
//#include "netloader.h"

#include <switch.h>

#include "util.h"

#define ERPT_SAVE_ID 0x80000000000000D1
#define TITLE_ID 0x4200000000000001
#define HEAP_SIZE 0x000540000 //0x000540000

// we aren't an applet
u32 __nx_applet_type = AppletType_None;

// setup a fake heap (we don't need the heap anyway)
char fake_heap[HEAP_SIZE];

extern void* __stack_top;//Defined in libnx.
#define STACK_SIZE 0x100000 //Change this if main-thread stack size ever changes.

// we override libnx internals to do a minimal init
void __libnx_initheap(void)
{
    extern char *fake_heap_start;
    extern char *fake_heap_end;

    // setup newlib fake heap
    fake_heap_start = fake_heap;
    fake_heap_end = fake_heap + HEAP_SIZE;
}

void __appInit(void)
{
    Result rc;
    svcSleepThread(10000000000L);
    rc = smInitialize();
    if (R_FAILED(rc))
        fatalLater(rc);
    rc = fsInitialize();
    if (R_FAILED(rc))
        fatalLater(rc);
    rc = fsdevMountSdmc();
    if (R_FAILED(rc))
        fatalLater(rc);
    rc = timeInitialize();
    if (R_FAILED(rc))
        fatalLater(rc);
}

void __appExit(void)
{
    fsdevUnmountAll();
    fsExit();
    smExit();
    audoutExit();
    timeExit();
}

static loop_status_t loop(loop_status_t (*callback)(void))
{
    loop_status_t status = LOOP_CONTINUE;

    while (appletMainLoop())
    {
        svcSleepThread(30000000L);
        status = callback();
        if (status != LOOP_CONTINUE)
            return status;
    }
    return LOOP_EXIT;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    mkdir("/logs", 0700);
    FILE *f = fopen("/logs/nxlink.log", "a");
    stdout = f;
    stderr = f;
    printf("\n---------------------------------\n");

    printf("Start!\n");

    loop_status_t status = LOOP_RESTART;

    while (status == LOOP_RESTART)
    {
        if (netloader_activate() == 0) // only run once.
        {
            printf("Server Active!\n");
            status = loop(netloader_loop);

            netloader_deactivate();
        }
        else
        {
            printf("Server Inactive!\n");
            status = LOOP_EXIT;
        }
    }

    printf("End Loop!\n");

    printf("Done!\n");

    fclose(f);

    return 0;
}
