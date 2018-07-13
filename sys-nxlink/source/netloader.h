#pragma once

/*! Loop status */
typedef enum
{
  LOOP_CONTINUE, /*!< Continue looping */
  LOOP_RESTART,  /*!< Reinitialize */
  LOOP_EXIT,     /*!< Terminate looping */
} loop_status_t;

//int loadnro(fileHolder *me, int sock, struct in_addr remote);
int           netloader_activate(void);
loop_status_t netloader_loop(void);
void          netloader_deactivate(void);
