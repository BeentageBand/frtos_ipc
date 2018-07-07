
#define COBJECT_IMPLEMENTATION
#define Dbg_FID DBG_FID_DEF(IPC_FID, 4)

#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include "dbg_log.h"
#include "frtos_ipc.h"
#include "ipc.h"

#define THREAD_INIT(tid, desc) -1,
static void * frtos_ipc_routine(void * thread);
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

frtos_ipc_Class_T FRTOS_IPC_Class =
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
static pthread_condattr_t FRTOS_Cond_Attr = PTHREAD_COND_INITIALIZER;
static pthread_attr_t FRTOS_Thread_Attr;
static pthread_mutexattr_t FRTOS_Mux_Attr;

static pthread_t FRTOS_Pool[IPC_MAX_TID] =
    {
   -1,
   IPC_THREAD_LIST(THREAD_INIT)
    };

void * frtos_ipc_routine(void * thread)
{
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
   pthread_exit(NULL);
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
  struct timespec timespec;
  int rc = clock_gettime(CLOCK_MONOTONIC, &timespec);
  return (rc)? 0 : (timespec.tv_sec * 1000 + timespec.tv_nsec / 1000000);
}

void frtos_ipc_sleep(union IPC_Helper * const helper, IPC_Clock_T const sleep_ms)
{
  usleep(sleep_ms * 1000);
}

bool frtos_ipc_is_time_elapsed(union IPC_Helper * const helper, IPC_Clock_T const time_ms)
{
  return (time_ms < helper->vtbl->time(helper));
}

IPC_TID_T frtos_ipc_self_thread(union IPC_Helper * const helper)
{
  IPC_TID_T i;
  pthread_t self = pthread_self();

  for(i = 0; i < IPC_MAX_TID; ++i)
    {
      if( pthread_equal(FRTOS_Pool[i], self))
   {
     break;
   }
    }

  return i;
}

bool frtos_ipc_alloc_thread(union IPC_Helper * const helper, union Thread * const thread)
{
  bool rc = true;

  if(-1 == FRTOS_Pool[thread->tid])
    {
      rc = true;
      thread->attr = &FRTOS_Thread_Attr;
      IPC_Register_Thread(thread);
    }
  return rc;
}

bool frtos_ipc_free_thread(union IPC_Helper * const helper, union Thread * const thread)
{
  bool rc = false;

  if(0 == pthread_cancel(FRTOS_Pool[thread->tid]))
    {
      FRTOS_Pool[thread->tid] = -1;
    }
  return rc;
}

bool frtos_ipc_run_thread(union IPC_Helper * const helper, union Thread * const thread)
{
  bool rc = false;
  if(-1 == FRTOS_Pool[thread->tid])
    {
      rc = 0 == pthread_create(FRTOS_Pool + thread->tid,
                (pthread_attr_t *)thread->attr,
                frtos_ipc_routine, (void *)thread);
    }
  return rc;
}

bool frtos_ipc_join_thread(union IPC_Helper * const helper, union Thread * const thread)
{
  bool rc = false;

  if(-1 == FRTOS_Pool[thread->tid])
    {
      union Thread * t = NULL;
      rc = 0 == pthread_join(FRTOS_Pool[thread->tid],
              (void **) &t);
    }

  return rc;
}

bool frtos_ipc_alloc_mutex(union IPC_Helper * const helper, union Mutex * const mutex)
{
  return 0 == pthread_mutex_init((pthread_mutex_t *)&mutex->mux, &FRTOS_Mux_Attr);
}

bool frtos_ipc_free_mutex(union IPC_Helper * const helper, union Mutex * const mutex)
{
  return 0 == pthread_mutex_destroy((pthread_mutex_t *)&mutex->mux);
}

bool frtos_ipc_lock_mutex(union IPC_Helper * const helper, union Mutex * const mutex,
           IPC_Clock_T const wait_ms)
{
  struct timespec timespec;
  frtos_ipc_make_timespec(&timespec, wait_ms);

  return 0 == pthread_mutex_timedlock((pthread_mutex_t *)&mutex->mux,
                  &timespec);
}

bool frtos_ipc_unlock_mutex(union IPC_Helper * const helper, union Mutex * const mutex)
{
  return 0 == pthread_mutex_unlock((pthread_mutex_t *)&mutex->mux);
}

bool frtos_ipc_alloc_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore,
                uint8_t const value)
{
  return 0 == sem_init((sem_t *)&semaphore->sem, 0, value);
}

bool frtos_ipc_free_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore)
{
  return 0 == sem_destroy((sem_t *)&semaphore->sem);
}

bool frtos_ipc_wait_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore,
               IPC_Clock_T const wait_ms)
{
  struct timespec timespec;
  frtos_ipc_make_timespec(&timespec, wait_ms);
  return 0 == sem_timedwait((sem_t *)&semaphore->sem, &timespec);
}

bool frtos_ipc_post_semaphore(union IPC_Helper * const helper, union Semaphore * const semaphore)
{
  return 0 == sem_post((sem_t *)&semaphore->sem);
}

bool frtos_ipc_alloc_conditional(union IPC_Helper * const helper, union Conditional * const conditional)
{
  return 0 == pthread_cond_init((pthread_cond_t *)&conditional->conditional,
            &FRTOS_Cond_Attr);
}

bool frtos_ipc_free_conditional(union IPC_Helper * const helper, union Conditional * const conditional)
{
  return 0 == pthread_cond_destroy((pthread_cond_t *)&conditional->conditional);
}

bool frtos_ipc_wait_conditional(union IPC_Helper * const helper, union Conditional * const conditional,
            union Mutex * const mutex, IPC_Clock_T const wait_ms)
{
  struct timespec timespec;
  frtos_ipc_make_timespec(&timespec, wait_ms);
  return 0 == pthread_cond_timedwait((pthread_cond_t *)&conditional->conditional,
                 (pthread_mutex_t *)&mutex->mux, &timespec);
}

bool frtos_ipc_post_conditional(union IPC_Helper * const helper, union Conditional * const conditional)
{
  union frtos_ipc * const this = _cast(FRTOS_IPC, helper);
  Isnt_Nullptr(this, false);
  return 0 == pthread_cond_signal((pthread_cond_t *)&conditional->conditional);
}

void frtos_ipc_make_timespec(struct timespec * const tm, IPC_Clock_T const clock_ms)
{
  tm->tv_sec = clock_ms / 1000;
  tm->tv_nsec = clock_ms - (tm->tv_sec * 1000);
  tm->tv_nsec *= 1000000;
}

void Populate_frtos_ipc(union FRTOS_IPC * const this)
{

  if(NULL == frtos_ipc.vtbl)
    {
      FRTOS_Pool[IPC_MAIN_TID] = pthread_self();
      Dbg_Warn("Start IPC FRTOS: starter thread %d is IPC_MAIN_TID", FRTOS_Pool[IPC_MAIN_TID]);
      Populate_IPC_Helper(&frtos_ipc.IPC_Helper);
      Object_Init(&frtos_ipc.Object, &FRTOS_IPC_Class.Class, 0);
      pthread_condattr_init(&FRTOS_Cond_Attr);
      pthread_attr_init(&FRTOS_Thread_Attr);
      pthread_mutexattr_init(&FRTOS_Mux_Attr);
    }
  memcpy(this, &frtos_ipc, sizeof(FRTOS_IPC));
}
