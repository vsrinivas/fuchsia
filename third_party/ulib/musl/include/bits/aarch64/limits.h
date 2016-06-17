#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) || \
    defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
#define LONG_BIT 64
#define PAGE_SIZE 4096
#endif

#define LONG_MAX 0x7fffffffffffffffL
#define LLONG_MAX 0x7fffffffffffffffLL
