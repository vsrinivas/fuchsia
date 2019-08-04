#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>

int sched_setaffinity(pid_t tid, size_t size, const cpu_set_t* set) {
  errno = ENOSYS;
  return -1;
}

int pthread_setaffinity_np(pthread_t td, size_t size, const cpu_set_t* set) { return ENOSYS; }

int sched_getaffinity(pid_t tid, size_t size, cpu_set_t* set) {
  errno = ENOSYS;
  return -1;
}

int pthread_getaffinity_np(pthread_t td, size_t size, cpu_set_t* set) { return ENOSYS; }
