#if !(__ARM_ARCH >= 7)
#error "must use -march=armv7-a"
#endif

#define a_ll a_ll
static inline int a_ll(volatile int* p) {
    int v;
    __asm__ __volatile__("ldrex %0, %1"
                         : "=r"(v)
                         : "Q"(*p));
    return v;
}

#define a_sc a_sc
static inline int a_sc(volatile int* p, int v) {
    int r;
    __asm__ __volatile__("strex %0,%2,%1"
                         : "=&r"(r), "=Q"(*p)
                         : "r"(v)
                         : "memory");
    return !r;
}

#define a_barrier a_barrier
static inline void a_barrier(void) {
    __asm__ __volatile__("dmb ish"
                         :
                         :
                         : "memory");
}

#define a_pre_llsc a_barrier
#define a_post_llsc a_barrier

#define a_crash __builtin_trap
