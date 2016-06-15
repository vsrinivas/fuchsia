#include <netdb.h>
#include <stdlib.h>

void freeaddrinfo(struct addrinfo* p) {
    free(p);
}
