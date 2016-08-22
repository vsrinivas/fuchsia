#define TLS_ABOVE_TP
static inline void* pthread_to_tp(struct pthread* thread) {
    return ((char*)(thread) + sizeof(struct pthread) - 8);
}
static inline struct pthread* tp_to_pthread(void* tp) {
    return (struct pthread*)((char*)tp + 8 - sizeof(struct pthread));
}

#define MC_PC arm_pc
