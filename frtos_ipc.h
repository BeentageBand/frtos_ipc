#ifndef FRTOS_IPC_H
#define FRTOS_IPC_H

#include "ipc_helper.h"

typedef union IPC_Helper_Class FRTOS_IPC_Class_T;

typedef union FRTOS_IPC
{
  FRTOS_IPC_Class_T _private * _private vtbl;
  struct Object Object;
  struct
  {
    union IPC_Helper IPC_Helper;
  };
}FRTOS_IPC_T;

extern FRTOS_IPC_Class_T FRTOS_IPC_Class;

extern void Populate_FRTOS_IPC(union FRTOS_IPC * const frtos);

#endif /*FRTOS_IPC_H*/
