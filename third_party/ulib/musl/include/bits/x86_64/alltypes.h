#if defined(__FLT_EVAL_METHOD__) && __FLT_EVAL_METHOD__ == 2
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

#if defined(__NEED_pthread_attr_t) && !defined(__DEFINED_pthread_attr_t)
typedef struct {
    const char* __name;
    int __c11;
    union {
        int __i[14];
        volatile int __vi[14];
        unsigned long __s[7];
    } __u;
} pthread_attr_t;
#define __DEFINED_pthread_attr_t
#endif

#if defined(__NEED_pthread_mutex_t) && !defined(__DEFINED_pthread_mutex_t)
typedef struct {
    union {
        int __i[10];
        volatile int __vi[10];
        volatile void* volatile __p[5];
    } __u;
} pthread_mutex_t;
#define __DEFINED_pthread_mutex_t
#endif

#if defined(__NEED_mtx_t) && !defined(__DEFINED_mtx_t)
typedef struct {
    int __i[1];
} mtx_t;
#define __DEFINED_mtx_t
#endif

#if defined(__NEED_pthread_cond_t) && !defined(__DEFINED_pthread_cond_t)
typedef struct {
    union {
        int __i[12];
        volatile int __vi[12];
        void* __p[6];
    } __u;
} pthread_cond_t;
#define __DEFINED_pthread_cond_t
#endif

#if defined(__NEED_cnd_t) && !defined(__DEFINED_cnd_t)
typedef struct {
    union {
        int __i[12];
        volatile int __vi[12];
        void* __p[6];
    } __u;
} cnd_t;
#define __DEFINED_cnd_t
#endif

#if defined(__NEED_pthread_rwlock_t) && !defined(__DEFINED_pthread_rwlock_t)
typedef struct {
    union {
        int __i[14];
        volatile int __vi[14];
        void* __p[7];
    } __u;
} pthread_rwlock_t;
#define __DEFINED_pthread_rwlock_t
#endif

#if defined(__NEED_pthread_barrier_t) && !defined(__DEFINED_pthread_barrier_t)
typedef struct {
    union {
        int __i[8];
        volatile int __vi[8];
        void* __p[4];
    } __u;
} pthread_barrier_t;
#define __DEFINED_pthread_barrier_t
#endif
