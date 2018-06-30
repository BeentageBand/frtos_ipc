#include "ipc_helper.h"

typedef union IPC_Helper_Class Free_RTOS_IPC_Class_T;

union Free_RTOS_IPC
{
    Free_RTOS_IPC_Class_T _private * _private vtbl;
    union IPC_Helper IPC_Helper;
    struct Object Object;
}Free_RTOS_IPC_T;

extern Free_RTOS_IPC_Class_T _private Free_RTOS_IPC_Class;

extern void Populate_Free_RTOS_IPC(union Free_RTOS_IPC * const frtos_ipc);