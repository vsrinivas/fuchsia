#include <sys/ioctl.h>
#include <sys/socket.h>

int sockatmark(int s) {
    int ret;
    if (ioctl(s, SIOCATMARK, &ret) < 0) return -1;
    return ret;
}
