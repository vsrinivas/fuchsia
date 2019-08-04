#if defined(__cplusplus) && !defined(__clang__)
#define __C11_ATOMIC(t) t
#else
#define __C11_ATOMIC(t) _Atomic(t)
#endif

#if defined(__NEED_uint8_t) && !defined(__DEFINED_uint8_t)
typedef __UINT8_TYPE__ uint8_t;
#define __DEFINED_uint8_t
#endif

#if defined(__NEED_uint16_t) && !defined(__DEFINED_uint16_t)
typedef __UINT16_TYPE__ uint16_t;
#define __DEFINED_uint16_t
#endif

#if defined(__NEED_uint32_t) && !defined(__DEFINED_uint32_t)
typedef __UINT32_TYPE__ uint32_t;
#define __DEFINED_uint32_t
#endif

#if defined(__NEED_uint64_t) && !defined(__DEFINED_uint64_t)
typedef __UINT64_TYPE__ uint64_t;
#define __DEFINED_uint64_t
#endif

#if defined(__NEED_int8_t) && !defined(__DEFINED_int8_t)
typedef __INT8_TYPE__ int8_t;
#define __DEFINED_int8_t
#endif

#if defined(__NEED_int16_t) && !defined(__DEFINED_int16_t)
typedef __INT16_TYPE__ int16_t;
#define __DEFINED_int16_t
#endif

#if defined(__NEED_int32_t) && !defined(__DEFINED_int32_t)
typedef __INT32_TYPE__ int32_t;
#define __DEFINED_int32_t
#endif

#if defined(__NEED_int64_t) && !defined(__DEFINED_int64_t)
typedef __INT64_TYPE__ int64_t;
#define __DEFINED_int64_t
#endif

#if defined(__NEED_uint_least8_t) && !defined(__DEFINED_uint_least8_t)
typedef __UINT_LEAST8_TYPE__ uint_least8_t;
#define __DEFINED_uint_least8_t
#endif

#if defined(__NEED_uint_least16_t) && !defined(__DEFINED_uint_least16_t)
typedef __UINT_LEAST16_TYPE__ uint_least16_t;
#define __DEFINED_uint_least16_t
#endif

#if defined(__NEED_uint_least32_t) && !defined(__DEFINED_uint_least32_t)
typedef __UINT_LEAST32_TYPE__ uint_least32_t;
#define __DEFINED_uint_least32_t
#endif

#if defined(__NEED_uint_least64_t) && !defined(__DEFINED_uint_least64_t)
typedef __UINT_LEAST64_TYPE__ uint_least64_t;
#define __DEFINED_uint_least64_t
#endif

#if defined(__NEED_int_least8_t) && !defined(__DEFINED_int_least8_t)
typedef __INT_LEAST8_TYPE__ int_least8_t;
#define __DEFINED_int_least8_t
#endif

#if defined(__NEED_int_least16_t) && !defined(__DEFINED_int_least16_t)
typedef __INT_LEAST16_TYPE__ int_least16_t;
#define __DEFINED_int_least16_t
#endif

#if defined(__NEED_int_least32_t) && !defined(__DEFINED_int_least32_t)
typedef __INT_LEAST32_TYPE__ int_least32_t;
#define __DEFINED_int_least32_t
#endif

#if defined(__NEED_int_least64_t) && !defined(__DEFINED_int_least64_t)
typedef __INT_LEAST64_TYPE__ int_least64_t;
#define __DEFINED_int_least64_t
#endif

#if defined(__NEED_uint_fast8_t) && !defined(__DEFINED_uint_fast8_t)
typedef __UINT_FAST8_TYPE__ uint_fast8_t;
#define __DEFINED_uint_fast8_t
#endif

#if defined(__NEED_uint_fast16_t) && !defined(__DEFINED_uint_fast16_t)
typedef __UINT_FAST16_TYPE__ uint_fast16_t;
#define __DEFINED_uint_fast16_t
#endif

#if defined(__NEED_uint_fast32_t) && !defined(__DEFINED_uint_fast32_t)
typedef __UINT_FAST32_TYPE__ uint_fast32_t;
#define __DEFINED_uint_fast32_t
#endif

#if defined(__NEED_uint_fast64_t) && !defined(__DEFINED_uint_fast64_t)
typedef __UINT_FAST64_TYPE__ uint_fast64_t;
#define __DEFINED_uint_fast64_t
#endif

#if defined(__NEED_int_fast8_t) && !defined(__DEFINED_int_fast8_t)
typedef __INT_FAST8_TYPE__ int_fast8_t;
#define __DEFINED_int_fast8_t
#endif

#if defined(__NEED_int_fast16_t) && !defined(__DEFINED_int_fast16_t)
typedef __INT_FAST16_TYPE__ int_fast16_t;
#define __DEFINED_int_fast16_t
#endif

#if defined(__NEED_int_fast32_t) && !defined(__DEFINED_int_fast32_t)
typedef __INT_FAST32_TYPE__ int_fast32_t;
#define __DEFINED_int_fast32_t
#endif

#if defined(__NEED_int_fast64_t) && !defined(__DEFINED_int_fast64_t)
typedef __INT_FAST64_TYPE__ int_fast64_t;
#define __DEFINED_int_fast64_t
#endif

#if defined(__NEED_intptr_t) && !defined(__DEFINED_intptr_t)
typedef __INTPTR_TYPE__ intptr_t;
#define __DEFINED_intptr_t
#endif

#if defined(__NEED_uintptr_t) && !defined(__DEFINED_uintptr_t)
typedef __UINTPTR_TYPE__ uintptr_t;
#define __DEFINED_uintptr_t
#endif

#if defined(__NEED_intmax_t) && !defined(__DEFINED_intmax_t)
typedef __INTMAX_TYPE__ intmax_t;
#define __DEFINED_intmax_t
#endif

#if defined(__NEED_uintmax_t) && !defined(__DEFINED_uintmax_t)
typedef __UINTMAX_TYPE__ uintmax_t;
#define __DEFINED_uintmax_t
#endif

#ifndef __cplusplus
#if defined(__NEED_wchar_t) && !defined(__DEFINED_wchar_t)
typedef __WCHAR_TYPE__ wchar_t;
#define __DEFINED_wchar_t
#endif
#endif

#if defined(__NEED_wint_t) && !defined(__DEFINED_wint_t)
typedef unsigned wint_t;
#define __DEFINED_wint_t
#endif

#if defined(__NEED_wctype_t) && !defined(__DEFINED_wctype_t)
typedef unsigned long wctype_t;
#define __DEFINED_wctype_t
#endif

#if defined(__NEED_size_t) && !defined(__DEFINED_size_t)
typedef __SIZE_TYPE__ size_t;
#define __DEFINED_size_t
#endif

#if defined(__NEED_ptrdiff_t) && !defined(__DEFINED_ptrdiff_t)
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#define __DEFINED_ptrdiff_t
#endif

#if defined(__NEED_va_list) && !defined(__DEFINED_va_list)
typedef __builtin_va_list va_list;
#define __DEFINED_va_list
#endif

#if defined(__NEED___isoc_va_list) && !defined(__DEFINED___isoc_va_list)
typedef __builtin_va_list __isoc_va_list;
#define __DEFINED___isoc_va_list
#endif

#if defined(__NEED_ssize_t) && !defined(__DEFINED_ssize_t)
typedef long ssize_t;
#define __DEFINED_ssize_t
#endif

#if defined(__NEED_time_t) && !defined(__DEFINED_time_t)
typedef long time_t;
#define __DEFINED_time_t
#endif

#if defined(__NEED_max_align_t) && !defined(__DEFINED_max_align_t)
typedef struct {
  long long __ll;
  long double __ld;
} max_align_t;
#define __DEFINED_max_align_t
#endif

#if defined(__x86_64__) && defined(__FLT_EVAL_METHOD__) && __FLT_EVAL_METHOD__ == 2
#if defined(__NEED_float_t) && !defined(__DEFINED_float_t)
typedef long double float_t;
#define __DEFINED_float_t
#endif

#if defined(__NEED_double_t) && !defined(__DEFINED_double_t)
typedef long double double_t;
#define __DEFINED_double_t
#endif

#else
#if defined(__NEED_float_t) && !defined(__DEFINED_float_t)
typedef float float_t;
#define __DEFINED_float_t
#endif

#if defined(__NEED_double_t) && !defined(__DEFINED_double_t)
typedef double double_t;
#define __DEFINED_double_t
#endif

#endif

#if defined(__NEED_suseconds_t) && !defined(__DEFINED_suseconds_t)
typedef long suseconds_t;
#define __DEFINED_suseconds_t
#endif

#if defined(__NEED_useconds_t) && !defined(__DEFINED_useconds_t)
typedef unsigned useconds_t;
#define __DEFINED_useconds_t
#endif

#if defined(__NEED_clockid_t) && !defined(__DEFINED_clockid_t)
typedef int clockid_t;
#define __DEFINED_clockid_t
#endif

#if defined(__NEED_clock_t) && !defined(__DEFINED_clock_t)
typedef long clock_t;
#define __DEFINED_clock_t
#endif

#if defined(__NEED_pid_t) && !defined(__DEFINED_pid_t)
typedef int pid_t;
#define __DEFINED_pid_t
#endif

#if defined(__NEED_id_t) && !defined(__DEFINED_id_t)
typedef unsigned id_t;
#define __DEFINED_id_t
#endif

#if defined(__NEED_uid_t) && !defined(__DEFINED_uid_t)
typedef unsigned uid_t;
#define __DEFINED_uid_t
#endif

#if defined(__NEED_gid_t) && !defined(__DEFINED_gid_t)
typedef unsigned gid_t;
#define __DEFINED_gid_t
#endif

#if defined(__NEED_register_t) && !defined(__DEFINED_register_t)
typedef long register_t;
#define __DEFINED_register_t
#endif

#if defined(__NEED_nlink_t) && !defined(__DEFINED_nlink_t)
typedef unsigned long nlink_t;
#define __DEFINED_nlink_t
#endif

#if defined(__NEED_off_t) && !defined(__DEFINED_off_t)
typedef long long off_t;
#define __DEFINED_off_t
#endif

#if defined(__NEED_ino_t) && !defined(__DEFINED_ino_t)
typedef unsigned long long ino_t;
#define __DEFINED_ino_t
#endif

#if defined(__NEED_dev_t) && !defined(__DEFINED_dev_t)
typedef unsigned long long dev_t;
#define __DEFINED_dev_t
#endif

#if defined(__NEED_blksize_t) && !defined(__DEFINED_blksize_t)
typedef long blksize_t;
#define __DEFINED_blksize_t
#endif

#if defined(__NEED_blkcnt_t) && !defined(__DEFINED_blkcnt_t)
typedef long long blkcnt_t;
#define __DEFINED_blkcnt_t
#endif

#if defined(__NEED_fsblkcnt_t) && !defined(__DEFINED_fsblkcnt_t)
typedef unsigned long long fsblkcnt_t;
#define __DEFINED_fsblkcnt_t
#endif

#if defined(__NEED_fsfilcnt_t) && !defined(__DEFINED_fsfilcnt_t)
typedef unsigned long long fsfilcnt_t;
#define __DEFINED_fsfilcnt_t
#endif

#if defined(__NEED_struct_iovec) && !defined(__DEFINED_struct_iovec)
struct iovec {
  void* iov_base;
  size_t iov_len;
};
#define __DEFINED_struct_iovec
#endif

#if defined(__NEED_struct_timeval) && !defined(__DEFINED_struct_timeval)
struct timeval {
  time_t tv_sec;
  suseconds_t tv_usec;
};
#define __DEFINED_struct_timeval
#endif

#if defined(__NEED_struct_timespec) && !defined(__DEFINED_struct_timespec)
struct timespec {
  time_t tv_sec;
  long tv_nsec;
};
#define __DEFINED_struct_timespec
#endif

#if defined(__NEED_key_t) && !defined(__DEFINED_key_t)
typedef int key_t;
#define __DEFINED_key_t
#endif

#if defined(__NEED_timer_t) && !defined(__DEFINED_timer_t)
typedef void* timer_t;
#define __DEFINED_timer_t
#endif

#if defined(__NEED_regoff_t) && !defined(__DEFINED_regoff_t)
typedef long regoff_t;
#define __DEFINED_regoff_t
#endif

#if defined(__NEED_socklen_t) && !defined(__DEFINED_socklen_t)
typedef unsigned socklen_t;
#define __DEFINED_socklen_t
#endif

#if defined(__NEED_sa_family_t) && !defined(__DEFINED_sa_family_t)
typedef unsigned short sa_family_t;
#define __DEFINED_sa_family_t
#endif

#if defined(__NEED_FILE) && !defined(__DEFINED_FILE)
typedef struct _IO_FILE FILE;
#define __DEFINED_FILE
#endif

#if defined(__NEED_locale_t) && !defined(__DEFINED_locale_t)
typedef struct __locale_struct* locale_t;
#define __DEFINED_locale_t
#endif

#if defined(__NEED_mode_t) && !defined(__DEFINED_mode_t)
typedef unsigned mode_t;
#define __DEFINED_mode_t
#endif

#if defined(__NEED_sigset_t) && !defined(__DEFINED_sigset_t)
typedef struct __sigset_t {
  unsigned long __bits[128 / sizeof(long)];
} sigset_t;
#define __DEFINED_sigset_t
#endif

#if defined(__NEED_pthread_once_t) && !defined(__DEFINED_pthread_once_t)
typedef __C11_ATOMIC(int) pthread_once_t;
#define __DEFINED_pthread_once_t
#endif

#if defined(__NEED_once_flag) && !defined(__DEFINED_once_flag)
typedef __C11_ATOMIC(int) once_flag;
#define __DEFINED_once_flag
#endif

#if defined(__NEED_pthread_key_t) && !defined(__DEFINED_pthread_key_t)
typedef unsigned pthread_key_t;
#define __DEFINED_pthread_key_t
#endif

#if defined(__NEED_pthread_spinlock_t) && !defined(__DEFINED_pthread_spinlock_t)
typedef __C11_ATOMIC(int) pthread_spinlock_t;
#define __DEFINED_pthread_spinlock_t
#endif

#if defined(__NEED_pthread_mutexattr_t) && !defined(__DEFINED_pthread_mutexattr_t)
typedef struct {
  unsigned __attr;
} pthread_mutexattr_t;
#define __DEFINED_pthread_mutexattr_t
#endif

#if defined(__NEED_pthread_condattr_t) && !defined(__DEFINED_pthread_condattr_t)
typedef struct {
  unsigned __attr;
} pthread_condattr_t;
#define __DEFINED_pthread_condattr_t
#endif

#if defined(__NEED_pthread_barrierattr_t) && !defined(__DEFINED_pthread_barrierattr_t)
typedef struct {
  unsigned __attr;
} pthread_barrierattr_t;
#define __DEFINED_pthread_barrierattr_t
#endif

#if defined(__NEED_pthread_rwlockattr_t) && !defined(__DEFINED_pthread_rwlockattr_t)
typedef struct {
  unsigned __attr[2];
} pthread_rwlockattr_t;
#define __DEFINED_pthread_rwlockattr_t
#endif

#ifdef __cplusplus
#if defined(__NEED_pthread_t) && !defined(__DEFINED_pthread_t)
typedef unsigned long pthread_t;
#define __DEFINED_pthread_t
#endif

#else
#if defined(__NEED_pthread_t) && !defined(__DEFINED_pthread_t)
typedef struct __pthread* pthread_t;
#define __DEFINED_pthread_t
#endif

#endif

#if defined(__NEED_mbstate_t) && !defined(__DEFINED_mbstate_t)
typedef struct __mbstate_t {
  unsigned __opaque1, __opaque2;
} mbstate_t;
#define __DEFINED_mbstate_t
#endif

#if defined(__NEED_pthread_attr_t) && !defined(__DEFINED_pthread_attr_t)
typedef struct {
  const char* __name;
  int __c11;
  size_t _a_stacksize;
  size_t _a_guardsize;
  void* _a_stackaddr;
  int _a_detach;
  int _a_sched;
  int _a_policy;
  int _a_prio;
} pthread_attr_t;
#define __DEFINED_pthread_attr_t
#endif

#if defined(__NEED_pthread_mutex_t) && !defined(__DEFINED_pthread_mutex_t)
typedef struct {
  unsigned _m_attr;
  __C11_ATOMIC(int)
  _m_lock;
  __C11_ATOMIC(int)
  _m_waiters;
  int _m_count;
} pthread_mutex_t;
#define __DEFINED_pthread_mutex_t
#endif

#if defined(__NEED_mtx_t) && !defined(__DEFINED_mtx_t)
typedef struct
#if defined(__clang__)
    __attribute__((__capability__("mutex")))
#endif
{
  int __i[1];
} mtx_t;
#define __DEFINED_mtx_t
#endif

#if defined(__NEED_pthread_cond_t) && !defined(__DEFINED_pthread_cond_t)
typedef struct {
  void* _c_head;
  int _c_clock;
  void* _c_tail;
  __C11_ATOMIC(int)
  _c_lock;
} pthread_cond_t;
#define __DEFINED_pthread_cond_t
#endif

#if defined(__NEED_cnd_t) && !defined(__DEFINED_cnd_t)
typedef struct {
  void* _c_head;
  int _c_clock;
  void* _c_tail;
  __C11_ATOMIC(int) _c_lock;
} cnd_t;
#define __DEFINED_cnd_t
#endif

#if defined(__NEED_pthread_rwlock_t) && !defined(__DEFINED_pthread_rwlock_t)
typedef struct {
  __C11_ATOMIC(int)
  _rw_lock;
  __C11_ATOMIC(int)
  _rw_waiters;
} pthread_rwlock_t;
#define __DEFINED_pthread_rwlock_t
#endif

#if defined(__NEED_pthread_barrier_t) && !defined(__DEFINED_pthread_barrier_t)
typedef struct {
  __C11_ATOMIC(int)
  _b_lock;
  __C11_ATOMIC(int)
  _b_waiters;
  unsigned int _b_limit;
  __C11_ATOMIC(int)
  _b_count;
  __C11_ATOMIC(int)
  _b_waiters2;
  void* _b_inst;
} pthread_barrier_t;
#define __DEFINED_pthread_barrier_t
#endif

#if defined(__NEED_sem_t) && !defined(__DEFINED_sem_t)
typedef struct {
  __C11_ATOMIC(int)
  _s_value;
  __C11_ATOMIC(int)
  _s_waiters;
} sem_t;
#define __DEFINED_sem_t
#endif
