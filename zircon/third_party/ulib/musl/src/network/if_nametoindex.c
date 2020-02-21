#define _GNU_SOURCE
#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

unsigned if_nametoindex(const char* name) {
  struct ifreq ifr;
  int fd, r;

  if ((fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) < 0) {
    return 0;
  }
  strncpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
  r = ioctl(fd, SIOCGIFINDEX, &ifr);

  int saved_errno = errno;
  if (close(fd) == -1) {
    fprintf(stderr, "if_nametoindex failed to close fd: %s\n", strerror(errno));
  }
  errno = saved_errno;

  return r < 0 ? 0 : ifr.ifr_ifindex;
}
