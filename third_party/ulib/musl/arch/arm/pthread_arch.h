#include <magenta/tlsroot.h>

#define TLS_ABOVE_TP
#define TP_ADJ(p) ((char*)(p) + sizeof(struct pthread) - 8)

#define MC_PC arm_pc
