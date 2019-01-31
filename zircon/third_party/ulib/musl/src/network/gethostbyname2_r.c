#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>

static int roundup(int x, int m) {
    return (((x + m - 1)) & ~(m - 1));
}

int gethostbyname2_r(const char* name, int af, struct hostent* h, char* buf, size_t buflen,
                     struct hostent** res, int* err) {
    *err = 0;
    *res = NULL;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = af;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *addrinfo;
    int eai = getaddrinfo(name, NULL, &hints, &addrinfo);
    if (eai != 0) {
        switch (eai) {
        case EAI_NONAME:
            *err = HOST_NOT_FOUND;
            break;
        case EAI_AGAIN:
            *err = TRY_AGAIN;
            break;
        default:
            *err = NO_RECOVERY;
            break;
        }
        return 0;
    }

    h->h_addrtype = af;
    h->h_length = (af == AF_INET) ? 4 : 16;

    int namelen = strlen(name);

    int n_addr = 0;
    for (struct addrinfo *ap = addrinfo; ap != NULL; ap = ap->ai_next) {
        ++n_addr;
    }

    size_t need = roundup(namelen + 1, sizeof(char*)); // h_name
    need += sizeof(char*);                             // h_aliases
    need += sizeof(char*) * (n_addr + 1);              // h_addr_list
    need += h->h_length * n_addr;                      // addrs that h_addr_list points to

    if (need > buflen) {
        freeaddrinfo(addrinfo);
        *err = -1;
        return ERANGE;
    }

    memcpy(buf, name, namelen + 1);
    h->h_name = buf;
    buf += roundup(namelen + 1, sizeof(char*));

    h->h_aliases = (char**)buf;
    h->h_aliases[0] = NULL;
    buf += sizeof(char*);

    h->h_addr_list = (char**)buf;
    buf += sizeof(char*) * (n_addr + 1);

    n_addr = 0;
    for (struct addrinfo *ap = addrinfo; ap != NULL; ap = ap->ai_next) {
        if (af == AF_INET) {
            struct sockaddr_in* sin = (struct sockaddr_in*)ap->ai_addr;
            memcpy(buf, &sin->sin_addr, h->h_length);
        } else if (af == AF_INET6) {
            struct sockaddr_in6* sin6 = (struct sockaddr_in6*)ap->ai_addr;
            memcpy(buf, &sin6->sin6_addr, h->h_length);
        }
        h->h_addr_list[n_addr++] = buf;
        buf += h->h_length;
    }
    h->h_addr_list[n_addr] = NULL;

    freeaddrinfo(addrinfo);
    *res = h;
    return 0;
}
