#include <assert.h>
#include <stdatomic.h>
#include "thread_pool.h"
#include "task.h"
#include "logger.h"
static atomic_int completed;
static void fast(void* ignored) { (void)ignored; atomic_fetch_add(&completed,1); }
int main(void) {
 logger_init(&g_logger,LOG_ERROR,stderr);
 for(int round=0;round<50;round++) {
  Error e; error_init(&e);
  ThreadPool* p=thread_pool_create(8,16,&e); assert(p);
  for(int i=0;i<2000;i++) { Task* t=task_create(i,i%11,fast,0); assert(t); assert(thread_pool_submit(p,t,&e)==0); }
  thread_pool_wait_idle(p);
  assert(thread_pool_get_metrics(p).tasks_completed==2000);
  thread_pool_destroy(p);
 }
 assert(atomic_load(&completed)==100000);
 logger_destroy(&g_logger);
}
