#include <sys/socket.h>

#include <stddef.h>

ssize_t recv(int fd, void* buf, size_t len, int flags) {
    return recvfrom(fd, buf, len, flags, NULL, NULL);
}
