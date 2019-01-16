#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

int main() {
  int ret;

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_addr = {
      .s_addr = htonl(INADDR_LOOPBACK),
    },
  };
  if ((ret = bind(server_fd, (const struct sockaddr*)&addr, sizeof(addr))) != 0) {
    fprintf(stderr, "bind failed: %s\n", strerror(errno));
    exit(1);
  }
  socklen_t addrLen = sizeof(addr);
  if ((ret = getsockname(server_fd, (struct sockaddr*)&addr, &addrLen)) != 0) {
    fprintf(stderr, "getsockname failed: %s\n", strerror(errno));
    exit(1);
  }
  if ((ret = listen(server_fd, 1)) != 0) {
    fprintf(stderr, "listen failed: %s\n", strerror(errno));
    exit(1);
  }

  int client_fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in bad_addr = addr;
  bad_addr.sin_port++;

  if ((ret = connect(client_fd, (const struct sockaddr*)&bad_addr, sizeof(bad_addr))) == 0) {
    fprintf(stderr, "bad connect succeeded\n");
    exit(1);
  }
  fprintf(stderr, "connect failed as expected: %s\n", strerror(errno));

  if ((ret = connect(client_fd, (const struct sockaddr*)&addr, sizeof(addr))) != 0) {
    fprintf(stderr, "connect failed: %s\n", strerror(errno));
    exit(1);
  }
}
