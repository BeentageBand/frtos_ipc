
#define COBJECT_IMPLEMENTATION
#define Dbg_FID DBG_FID_DEF(IPC_FID, 4)

#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include "dbg_log.h"
#include "frtos_ipc.h"
#include "ipc.h"

#define FRTOS_THREAD_STACK_SIZE (1024 UL)
#define THREAD_INIT(tid, desc) {NULL, 0, #tid, NULL, FRTOS_THREAD_STACK_SIZE}

typedef uint32_t FRTOS_Priority_T;

struct FRTOS_Thread
{
  TaskHandle_t handle;
  FRTOS_Priority_T priority;
  char * name;
  void * stack;
  size_t stack_size;
};

static void frtos_ipc_routine(void * param);
static void frtos_ipc_delete(struct Object * const obj);

static IPC_Clock_T frtos_ipc_time(union IPC_Helper * const helper);
static void frtos_ipc_sleep(union IPC_Helper * const helper, IPC_Clock_T const sleep_ms);
static bool frtos_ipc_is_time_elapsed(union IPC_Helper * const helper, IPC_Clock_T const time_ms);

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

static void frtos_ipc_make_timespec(struct timespec * const tm, IPC_Clock_T const clock_ms);

FRTOS_IPC_Class_T FRTOS_IPC_Class =
    {{
   { frtos_ipc_delete, NULL},
   frtos_ipc_time,
   frtos_ipc_sleep,
   frtos_ipc_is_time_elapsed,

   frtos_ipc_self_thread,
   frtos_ipc_alloc_thread,
   frtos_ipc_free_thread,
   frtos_ipc_run_thread,
   frtos_ipc_join_thread,

   frtos_ipc_alloc_mutex,
   frtos_ipc_free_mutex,
   frtos_ipc_lock_mutex,
   frtos_ipc_unlock_mutex,

   frtos_ipc_alloc_semaphore,
   frtos_ipc_free_semaphore,
   frtos_ipc_wait_semaphore,
   frtos_ipc_post_semaphore,

   frtos_ipc_alloc_conditional,
   frtos_ipc_free_conditional,
   frtos_ipc_wait_conditional,
   frtos_ipc_post_conditional
    }};

static union frtos_ipc FRTOS_IPC = {NULL};
static struct FRTOS_Thread FRTOS_Pool[IPC_MAX_TID] =
{
   {NULL, "ID"},
   IPC_THREAD_LIST(THREAD_INIT)
};

void frtos_ipc_routine(void * params)
{
   struct FRTOS_Thread * const thread = (struct FRTOS_Thread *) params;
   union Thread * const this = _cast(Thread, (union Thread *)thread);

   Isnt_Nullptr(this, NULL);

   Dbg_Info("%s for thread = %d", __func__, this->tid);

   if(this->vtbl && this->vtbl->runnable)
   {
      this->vtbl->runnable(this);
   }
   else
   {
      Dbg_Fault("%s:Unable to run thread %d", __func__, this->tid);
   }

   union Semaphore * t_sem = &this->sem_ready;
   t_sem->vtbl->post(t_sem);// thread ended wait
   pthread_exit(NULL);//TODO delete task
   return NULL;
}

void frtos_ipc_delete(struct Object * const obj)
{
  IPC_TID_T i;

  for(i = 0; i < IPC_MAX_MID; ++i)
    {
      if( -1 != FRTOS_Pool[i])
   {
     union Thread * thread = IPC_Helper_find_thread(i);
     if(thread)
       {
         _delete(thread);
       }
   }
    }

  pthread_condattr_destroy(&FRTOS_Cond_Attr);
  pthread_attr_destroy(&FRTOS_Thread_Attr);
  pthread_mutexattr_destroy(&FRTOS_Mux_Attr);

}

IPC_Clock_T frtos_ipc_time(union IPC_Helper * const helper)
{
   volatile UBaseType_t array;
   uint32_t uptime;
   array = uxTaskGetSystemState(NULL, 0, &uptime);
   return frtos_ipc_make_clock(uptime);
}

void frtos_ipc_sleep(union IPC_Helper * const helper, IPC_Clock_T const sleep_ms)
{
   TickType_t const sleep_ticks = frtos_ipc_make_tick(sleep_ms);
   vTaskDelay(sleep_ticks);
}

bool frtos_ipc_is_time_elapsed(union IPC_Helper * const helper, IPC_Clock_T const time_ms)
{
  return (time_ms < helper->vtbl->time(helper));
}

IPC_TID_T frtos_ipc_self_thread(union IPC_Helper * const helper)
{
  IPC_TID_T tid;
  TaskHandle_t handle = xTaskGetCurrentTaskHandle();
  tid = (IPC_TID_T) xTaskGetApplicationTaskTag(handle);
  return (IPC_MAX_TID > tid)? tid : IPC_MAX_TID;
}

bool frtos_ipc_alloc_thread(union IPC_Helper * const helper, union Thread * const thread)
{
  bool rc = true;

  if(NULL == FRTOS_Pool[thread->tid].stack)
  {
     rc = true;
     thread->attr = (FRTOS_Pool + thread->tid);
     FRTOS_Pool[thread->tid].stack = malloc(FRTOS_Thread_Attr.stack_size);
     FRTOS_Pool[thread->tid].stack_size = FRTOS_Thread_Attr.stack_size;
     IPC_Register_Thread(thread);
  }
  return rc;
}

bool frtos_ipc_free_thread(union IPC_Helper * const helper, union Thread * const thread)
{
  bool rc = false;

  if(thread->attr  == (FRTOS_Pool + thread->tid))
  {
     vTaskDelete(FRTOS_Pool[thread->tid].handle);
     free(FRTOS_Pool[thread->tid].stack);
     FRTOS_Pool[thread->tid].stack_size = 0;
  }
  return rc;
}

bool frtos_ipc_run_thread(union IPC_Helper * const helper, union Thread * const thread)
{
  BaseType_t rc = pdPASS;
  if(NULL == FRTOS_Pool[thread->tid].handle)
    {
      rc = 0 == xTaskCreate(
           frtos_ipc_routine,
           FRTOS_Pool[thread->tid].name,
           thread,
           FRTOS_Pool[thread->tid].priority,
           FRTOS_Pool[thread->tid].handle);
    }
  return rc == pdPASS;
}

bool frtos_ipc_join_thread(union IPC_Helper * const helper, union Thread * const thread)
{
  bool rc = false;
  //TODO Join
  return rc;
}

bool frtos_ipc_alloc_mutex(union IPC_Helper * const helper, union Mutex * const mutex)
{
   if(NULL == mutex->mux)
   {
      mutex->mux = xSemaphoteCreateMutex();
   }
   return 0 == mutex->mux;
}

bool frtos_ipc_free_mutex(union IPC_Helper * const helper, union Mutex * const mutex)
{
  return NULL == vSemaphoreDelete((SemaphoreHandle_t )mutex->mux);
}

bool frtos_ipc_lock_mutex(union IPC_Helper * const helper, union Mutex * const mutex,
           IPC_Clock_T const wait_ms)
{
  Isnt_Nullptr(mutex->mux, false);
  TickType_t wait_ticks = frtos_ipc_make_ticks(wait_ms);
  return pdTRUE == xSemaphoreTake((SemaphoreHandle_t) mutex->mux, wait_ticks);
}

bool frtos_ipc_unlock_mutex(union IPC_Helper * const helper, union Mutex * const mutex)
{
  Isnt_Nullptr(mutex->mux, false);
  return  pdTrue == xSemaphoreGive((SemaphoreHandle_t)mutex->mux);
}

bool frtos_ipc_alloc_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore,
                uint8_t const value)
{
  bool rc = false;
  if (NULL == semaphore->sem)
  {
    switch(value)
    {
      0 :
      rc = false;
      break;
      1 :
      semaphore->sem = (void *) xSemaphoreCreateBinary();
      rc = NULL != semaphore->sem;
      break;
      default:
      semaphore->sem = (void *) xSemaphoreCreateCounting(value, value);
      rc = NULL != semaphore->sem;
      break;
    }
  }
  return rc;
}

bool frtos_ipc_free_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore)
{
  return NULL == vSemaphoreDelete((SemaphoreHandle_t )semaphore->sem);
}

bool frtos_ipc_wait_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore,
               IPC_Clock_T const wait_ms)
{
  Isnt_Nullptr(semaphore->sem, false);
  TickType_t wait_ticks = frtos_ipc_make_ticks(wait_ms);
  return pdTRUE == xSemaphoreTake((SemaphoreHandle_t) semaphore->sem, wait_ticks);
}

bool frtos_ipc_post_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore)
{
  Isnt_Nullptr(semaphore->sem, false);
  return  pdTrue == xSemaphoreGive((SemaphoreHandle_t)semaphore->sem);
}

bool frtos_ipc_alloc_conditional(union IPC_Helper * const helper, union Conditional * const conditional)
{
  return 0; //TODO
}

bool frtos_ipc_free_conditional(union IPC_Helper * const helper, union Conditional * const conditional)
{
  return 0; //TODO
}

bool frtos_ipc_wait_conditional(union IPC_Helper * const helper, union Conditional * const conditional,
            union Mutex * const mutex, IPC_Clock_T const wait_ms)
{
  return 0; //TODO
}

bool frtos_ipc_post_conditional(union IPC_Helper * const helper, union Conditional * const conditional)
{
  return 0; //TODO
}

IPC_Clock_T frtos_ipc_make_clock(TickType_t const clock_ticks)
{
  return (IPC_Clock_T)(clock_ticks * FRTOS_MS_PER_CLOCK);

}

TickType_t frtos_ipc_make_ticks(IPC_Clock_T const clock_ms)
{
  return (TickType_t)(clock_ticks / FRTOS_MS_PER_CLOCK);
}

void Populate_frtos_ipc(union FRTOS_IPC * const this)
{

  if(NULL == FRTOS_IPC.vtbl)
    {
      Populate_IPC_Helper(&FRTOS_IPC.IPC_Helper);
      Object_Init(&frtos_ipc.Object, &FRTOS_IPC_Class.Class, 0);
      FRTOS_Pool[IPC_MAIN_TID] = FRTOS_IPC.vtbl->self_thread(&FRTOS_IPC);
      Dbg_Warn("Start IPC FRTOS: starter thread %d is IPC_MAIN_TID", FRTOS_Pool[IPC_MAIN_TID]);
    }
  memcpy(this, &FRTOS_IPC, sizeof(FRTOS_IPC));
}
