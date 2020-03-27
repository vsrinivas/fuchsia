#define _GNU_SOURCE
#include <errno.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

char *if_indextoname(unsigned index, char *name) {
  struct ifreq ifr;
  int fd, r;

  if ((fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) < 0)
    return 0;
  ifr.ifr_ifindex = index;
  r = ioctl(fd, SIOCGIFNAME, &ifr);
  close(fd);
  if (r < 0) {
    if (errno == ENODEV)
      errno = ENXIO;
    return 0;
  }
  return strncpy(name, ifr.ifr_name, IF_NAMESIZE);
}
