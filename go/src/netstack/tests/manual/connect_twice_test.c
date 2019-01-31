#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

int main() {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    fprintf(stderr, "socket failed: %s\n", strerror(errno));
    exit(1);
  }

  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_addr = {
      .s_addr = htonl(INADDR_LOOPBACK),
    },
  };
  if (bind(server_fd, (const struct sockaddr*)&addr, sizeof(addr)) != 0) {
    fprintf(stderr, "bind failed: %s\n", strerror(errno));
    exit(1);
  }
  socklen_t addrLen = sizeof(addr);
  if (getsockname(server_fd, (struct sockaddr*)&addr, &addrLen) != 0) {
    fprintf(stderr, "getsockname failed: %s\n", strerror(errno));
    exit(1);
  }
  if (listen(server_fd, 1) != 0) {
    fprintf(stderr, "listen failed: %s\n", strerror(errno));
    exit(1);
  }

  int client_fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in bad_addr = addr;
  bad_addr.sin_port++;

  if (connect(client_fd, (const struct sockaddr*)&bad_addr, sizeof(bad_addr)) == 0) {
    fprintf(stderr, "bad connect succeeded\n");
    exit(1);
  }
  fprintf(stderr, "connect failed as expected: %s\n", strerror(errno));

  if (connect(client_fd, (const struct sockaddr*)&addr, sizeof(addr)) != 0) {
    fprintf(stderr, "connect failed: %s\n", strerror(errno));
    exit(1);
  }
}
