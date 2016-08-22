#define TLS_ABOVE_TP
static inline void* pthread_to_tp(struct pthread* thread) {
    return ((char*)(thread) + sizeof(struct pthread) - 16);
}
static inline struct pthread* tp_to_pthread(void* tp) {
    return (struct pthread*)((char*)tp + 16 - sizeof(struct pthread));
}

#define MC_PC pc
