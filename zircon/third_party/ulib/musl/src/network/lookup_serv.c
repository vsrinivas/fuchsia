#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#include "lookup.h"
#include "stdio_impl.h"

static unsigned long lookup_port(const char* name, int proto) {
  if (!strcmp(name, "echo")) {
    return 7;
  };
  if (!strcmp(name, "ftp") && proto == IPPROTO_TCP) {
    return 21;
  };
  if (!strcmp(name, "ssh") && proto == IPPROTO_TCP) {
    return 22;
  };
  if (!strcmp(name, "telnet") && proto == IPPROTO_TCP) {
    return 23;
  };
  if (!strcmp(name, "tftp") && proto == IPPROTO_UDP) {
    return 69;
  };
  if (!strcmp(name, "http") && proto == IPPROTO_TCP) {
    return 80;
  };
  if (!strcmp(name, "ntp")) {
    return 123;
  };
  if (!strcmp(name, "imap") && proto == IPPROTO_TCP) {
    return 143;
  };
  if (!strcmp(name, "irc")) {
    return 194;
  };
  if (!strcmp(name, "ldap")) {
    return 389;
  };
  if (!strcmp(name, "https") && proto == IPPROTO_TCP) {
    return 443;
  };
  return 0;
}

int __lookup_serv(struct service buf[static MAXSERVS], const char* name, int proto, int socktype,
                  int flags) {
  int cnt = 0;
  char* z = (char*)"";
  unsigned long port = 0;

  switch (socktype) {
    case SOCK_STREAM:
      switch (proto) {
        case 0:
          proto = IPPROTO_TCP;
        case IPPROTO_TCP:
          break;
        default:
          return EAI_SERVICE;
      }
      break;
    case SOCK_DGRAM:
      switch (proto) {
        case 0:
          proto = IPPROTO_UDP;
        case IPPROTO_UDP:
          break;
        default:
          return EAI_SERVICE;
      }
    case 0:
      break;
    default:
      if (name)
        return EAI_SERVICE;
      buf[0].port = 0;
      buf[0].proto = proto;
      buf[0].socktype = socktype;
      return 1;
  }

  if (name) {
    if (!*name)
      return EAI_SERVICE;
    port = strtoul(name, &z, 10);
  }
  if (!*z) {
    if (port > 65535)
      return EAI_SERVICE;
    if (proto != IPPROTO_UDP) {
      buf[cnt].port = port;
      buf[cnt].socktype = SOCK_STREAM;
      buf[cnt++].proto = IPPROTO_TCP;
    }
    if (proto != IPPROTO_TCP) {
      buf[cnt].port = port;
      buf[cnt].socktype = SOCK_DGRAM;
      buf[cnt++].proto = IPPROTO_UDP;
    }
    return cnt;
  }

  if (flags & AI_NUMERICSERV)
    return EAI_SERVICE;

  port = lookup_port(name, proto);
  if (!port) {
    return EAI_SERVICE;
  }

  buf[cnt].port = port;
  buf[cnt].socktype = socktype;
  buf[cnt++].proto = proto;

  return cnt > 0 ? cnt : EAI_SERVICE;
}
