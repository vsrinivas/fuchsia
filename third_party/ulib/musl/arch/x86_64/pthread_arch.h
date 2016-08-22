static inline void* pthread_to_tp(struct pthread* thread) {
    return thread;
}
static inline struct pthread* tp_to_pthread(void* tp) {
    return tp;
}

#define MC_PC gregs[REG_RIP]
