#include "ifree_rtos.h"

static void frtos_ipc_delete(struct Object * const obj);

static IPC_Clock_T frtos_ipc_time(union IPC_Helper * const);
static void frtos_ipc_sleep(union IPC_Helper * const, IPC_Clock_T const sleep_ms);
static bool frtos_ipc_is_time_elapsed(union IPC_Helper * const, IPC_Clock_T const time_ms);

static IPC_TID_T frtos_ipc_self_thread(union IPC_Helper * const helper);
static bool frtos_ipc_alloc_thread(union IPC_Helper * const helper, union Thread * const thread);
static bool frtos_ipc_free_thread(union IPC_Helper * const helper, union Thread * const thread);
static bool frtos_ipc_run_thread(union IPC_Helper * const helper, union Thread * const thread);
static bool frtos_ipc_join_thread(union IPC_Helper * const helper, union Thread * const thread);

static bool frtos_ipc_alloc_mutex(union IPC_Helper * const helper, union Mutex * const mutex);
static bool frtos_ipc_free_mutex(union IPC_Helper * const helper, union Mutex * const mutex);
static bool frtos_ipc_lock_mutex(union IPC_Helper * const helper, union Mutex * const mutex,
		IPC_Clock_T const wait_ms);
static bool frtos_ipc_unlock_mutex(union IPC_Helper * const helper, union Mutex * const mutex);

static bool frtos_ipc_alloc_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore,
		uint8_t const value);
static bool frtos_ipc_free_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore);
static bool frtos_ipc_wait_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore, 
		IPC_Clock_T const wait_ms);
static bool frtos_ipc_post_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore);

static bool frtos_ipc_alloc_conditional(union IPC_Helper * const helper, union Conditional * const conditional);
static bool frtos_ipc_free_conditional(union IPC_Helper * const helper, union Conditional * const conditional);
static bool frtos_ipc_wait_conditional(union IPC_Helper * const helper, union Conditional * const conditional,
		union Mutex * const mutex, IPC_Clock_T const wait_ms);
static bool frtos_ipc_post_conditional(union IPC_Helper * const helper, union Conditional * const conditional);

struct IFree_RTOS_Thread IFree_RTOS_Thread_Pool[IPC_MAX_TID] =
{

};

void frtos_ipc_delete(struct Object * const obj){}

IPC_Clock_T frtos_ipc_time(union IPC_Helper * const h)
{
    unsigned long runtime;
    volatile UBaseType_t array_size;
    array_size = uxTaskGetSystemState( NULL,
                                 0,
                                 &runtime);
    return (IPC_Clock_T)runtime;
}

void frtos_ipc_sleep(union IPC_Helper * const h, IPC_Clock_T const sleep_ms)
{ 
    TickType_t ticks = frtos_ipc_make_ticks_from_ms(sleep_ms)
    vTaskDelay(ticks);
}

bool frtos_ipc_is_time_elapsed(union IPC_Helper * const h, IPC_Clock_T const time_ms)
{
    return h->vtbl->sleep(h) > time_ms;
}

IPC_TID_T frtos_ipc_self_thread(union IPC_Helper * const helper)
{
    TaskHandle_t curr_task = xTaskGetCurrentTaskHandle();
    IPC_TID_T self_tid = (IPC_TID_T) xTaskGetApplicationTaskTag(curr_task);
    return self_tid;
}

bool frtos_ipc_alloc_thread(union IPC_Helper * const helper, union Thread * const thread)
{
    if(NULL == IFree_RTOS_Thread_Pool[thread->tid].thread)
    {
        IFree_RTOS_Thread_Pool[thread->tid].stack_size = FRTOS_STACK_SIZE;
        IFree_RTOS_Thread_Pool[thread->tid].priority = FRTOS_PRIORITY;
        IFree_RTOS_Thread_Pool[thread->tid].thread = thread;
    }
}

bool frtos_ipc_free_thread(union IPC_Helper * const helper, union Thread * const thread)
{
    BaseType_T rc;
    if(thread == IFree_RTOS_Thread_Pool[thread->tid].thread)
    {
        rc = xTaskDelete(IFree_RTOS_Thread_Pool[thread->tid].handle);
    }
    return pdPASS == rc;
}

bool frtos_ipc_run_thread(union IPC_Helper * const helper, union Thread * const thread)
{
    BaseType_T rc;
    if(thread == IFree_RTOS_Thread_Pool[thread->tid].thread)
    {
        rc = xTaskCreate(frtos_ipc_routine,
                IFree_RTOS_Thread_Pool[thread->tid].name,
                IFree_RTOS_Thread_Pool[thread->tid].stack_size,
                (void *)IFree_RTOS_Thread_Pool + thread->tid,
                IFree_RTOS_Thread_Pool[thread->tid].priority,
                IFree_RTOS_Thread_Pool[thread->tid].handle);
    }
    return pdPASS == rc;
}

bool frtos_ipc_join_thread(union IPC_Helper * const helper, union Thread * const thread)
{}

bool frtos_ipc_alloc_mutex(union IPC_Helper * const helper, union Mutex * const mutex)
{
    if(NULL == mutex->mux)
        mutex->mux = xSemaphoreCreateMutex();
    return NULL != mutex->mux;
}

bool frtos_ipc_free_mutex(union IPC_Helper * const helper, union Mutex * const mutex)
{
    SemaphoreHandle_T sem = (SemaphoreHandle_T) mutex->mux;
    vSemaphoreDelete(sem);
    mutex->mux = NULL;
    return true;
}

bool frtos_ipc_lock_mutex(union IPC_Helper * const helper, union Mutex * const mutex,
		IPC_Clock_T const wait_ms)
{
    TickType_t wait_ticks = 
    SemaphoreHandle_T sem = (SemaphoreHandle_T) mutex->mux;
    Isnt_Nullptr(sem, FALSE);
    return xSemaphoreTake( sem, wait_ticks ) == pdTRUE;        
}

bool frtos_ipc_unlock_mutex(union IPC_Helper * const helper, union Mutex * const mutex)
{
    SemaphoreHandle_T sem = (SemaphoreHandle_T) mutex->mux;
    return xSemaphoreGive(sem) == pdTRUE;        
}

bool frtos_ipc_alloc_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore,
		uint8_t const value)
{
    if(NULL == semaphore->sem)
        semaphore->sem = xSemaphoreCreateCounting(FRTOS_MAX_COUNT, value);
    return NULL != semaphore->sem;
}

bool frtos_ipc_free_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore);
{
    SemaphoreHandle_T sem = (SemaphoreHandle_T) semaphore->sem;
    vSemaphoreDelete(sem);
    semaphore->sem = NULL;
    return true;
}

bool frtos_ipc_wait_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore, 
		IPC_Clock_T const wait_ms)
{
    TickType_t wait_ticks = 
    SemaphoreHandle_T sem = (SemaphoreHandle_T) semaphore->sem;
    Isnt_Nullptr(sem, FALSE);
    return xSemaphoreTake( sem, wait_ticks ) == pdTRUE;        
}

bool frtos_ipc_post_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore)
{
    SemaphoreHandle_T sem = (SemaphoreHandle_T) semaphore->sem;
    return xSemaphoreGive(sem) == pdTRUE;        
}

static bool frtos_ipc_alloc_conditional(union IPC_Helper * const helper, union Conditional * const conditional);
static bool frtos_ipc_free_conditional(union IPC_Helper * const helper, union Conditional * const conditional);
static bool frtos_ipc_wait_conditional(union IPC_Helper * const helper, union Conditional * const conditional,
		union Mutex * const mutex, IPC_Clock_T const wait_ms);
static bool frtos_ipc_post_conditional(union IPC_Helper * const helper, union Conditional * const conditional);

void free_rtos_task_routine(void * params)
{
    struct IFree_RTOS_Thread * const this = (struct IFree_RTOS_Thread *) params;
    union Thread * const thread = _cast(thread, (union Thread *)this->thread);
     vTaskSetApplicationTaskTag( NULL, ( void * ) thread->tid );
    if(NULL != thread)
    {
        thread->vtbl->runnable(thread);
    }
}