#define _GNU_SOURCE
#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

char* if_indextoname(unsigned index, char* name) {
  struct ifreq ifr;
  int fd, r;

  if ((fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) < 0)
    return 0;
  ifr.ifr_ifindex = index;
  r = ioctl(fd, SIOCGIFNAME, &ifr);
  if (r < 0 && errno == ENODEV) {
    errno = ENXIO;  // to match Linux, see `man if_indextoname`.
  }

  int saved_errno = errno;
  if (close(fd) == -1) {
    fprintf(stderr, "if_indextoname failed to close fd: %s\n", strerror(errno));
  }
  errno = saved_errno;

  return r < 0 ? 0 : strncpy(name, ifr.ifr_name, IF_NAMESIZE);
}
