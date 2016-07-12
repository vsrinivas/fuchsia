#include <magenta/tlsroot.h>

#define TLS_ABOVE_TP
#define TP_ADJ(p) ((char*)(p) + sizeof(struct pthread) - 16)

#define MC_PC pc
