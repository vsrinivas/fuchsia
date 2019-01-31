#define _GNU_SOURCE

#include <netdb.h>
#include <sys/socket.h>

int gethostbyname_r(const char* name, struct hostent* h, char* buf, size_t buflen,
                    struct hostent** res, int* err) {
    return gethostbyname2_r(name, AF_INET, h, buf, buflen, res, err);
}
