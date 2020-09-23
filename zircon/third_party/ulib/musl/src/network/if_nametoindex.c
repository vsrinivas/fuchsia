#define _GNU_SOURCE
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

unsigned if_nametoindex(const char *name) {
  struct ifreq ifr;
  int fd, r;

  if ((fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) < 0)
    return 0;

  // This intentionally may leave the array without a NUL terminator.  The
  // expected protocol exactly matches the strncpy semantics: there is a NUL
  // terminator if the string is shorter than the whole array size.  Usually
  // strncpy is dangerous when used like this since the usual protocol is to
  // always have a NUL terminator, so the compiler will warn when it notices
  // the maximum size doesn't leave space in the array for a NUL terminator.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
  strncpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
#pragma GCC diagnostic pop

  r = ioctl(fd, SIOCGIFINDEX, &ifr);
  close(fd);
  return r < 0 ? 0 : ifr.ifr_ifindex;
}
