#define _GNU_SOURCE

#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

struct hostent* gethostbyname(const char* name) {
    return gethostbyname2(name, AF_INET);
}
