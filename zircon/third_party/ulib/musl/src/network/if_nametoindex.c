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
  strncpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
  r = ioctl(fd, SIOCGIFINDEX, &ifr);
  close(fd);
  return r < 0 ? 0 : ifr.ifr_ifindex;
}
