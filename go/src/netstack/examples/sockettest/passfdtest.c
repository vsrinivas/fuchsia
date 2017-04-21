// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <launchpad/launchpad.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#undef CAN_CLONE_SOCKETS

static const char* sa_to_str(const struct sockaddr* sa,
                             char* str,
                             size_t strlen) {
  if (sa->sa_family == AF_INET) {
    struct sockaddr_in* sin = (struct sockaddr_in*)sa;
    return inet_ntop(AF_INET, &sin->sin_addr, str, strlen);
  } else if (sa->sa_family == AF_INET6) {
    struct sockaddr_in6* sin6 = (struct sockaddr_in6*)sa;
    return inet_ntop(AF_INET6, &sin6->sin6_addr, str, strlen);
  } else {
    return NULL;
  }
}

const char* kProgram = "/system/bin/passfdtest";

static int server(const char* service) {
  int16_t port = atoi(service);
  printf("listen on port %d\n", port);

  int s = socket(AF_INET6, SOCK_STREAM, 0);
  if (s < 0) {
    printf("socket failed (errno = %d)\n", errno);
    return -1;
  }

  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = in6addr_any;  // works with IPv4
  addr.sin6_port = htons(port);

  if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    printf("bind failed (errno = %d)\n", errno);
    return -1;
  }

  if (listen(s, 1) < 0) {
    printf("listen failed\n");
    return -1;
  }

  printf("waiting for a connection on port %d...\n", port);
  socklen_t addrlen = sizeof(addr);
  int conn = accept(s, (struct sockaddr*)&addr, &addrlen);
  if (conn < 0) {
    close(s);
    printf("accept failed (errno = %d)\n", errno);
    return -1;
  }
  char str[INET6_ADDRSTRLEN];
  printf("connected from %s\n",
         sa_to_str((struct sockaddr*)&addr, str, sizeof(str)));

  launchpad_t* lp;
  launchpad_create(mx_job_default(), kProgram, &lp);
  const char* argv[2] = {kProgram, "ECHO"};
  launchpad_load_from_file(lp, kProgram);
  launchpad_set_args(lp, 2, argv);
  launchpad_clone(lp, LP_CLONE_MXIO_ROOT | LP_CLONE_MXIO_CWD);
#ifdef CAN_CLONE_SOCKETS
  launchpad_clone_fd(lp, conn, STDIN_FILENO);
  launchpad_transfer_fd(lp, conn, STDOUT_FILENO);
#else
  launchpad_transfer_fd(lp, conn, STDIN_FILENO);
  launchpad_clone_fd(lp, STDOUT_FILENO, STDOUT_FILENO);
#endif
  launchpad_clone_fd(lp, STDERR_FILENO, STDERR_FILENO);

  mx_handle_t proc = 0;
  const char* errmsg;

  mx_status_t status = launchpad_go(lp, &proc, &errmsg);
  if (status < 0) {
    fprintf(stderr, "error from launchpad_go: %s\n", errmsg);
    return -1;
  }

  printf("launched %s %s, waiting for it to exit...\n", argv[0], argv[1]);
  mx_signals_t observed;
  mx_object_wait_one(proc, MX_PROCESS_SIGNALED, MX_TIME_INFINITE, &observed);

  printf("child exited.\n");


  close(s);

  return 0;
}

int echo() {
  fprintf(stderr, "ECHO starting\n");
#ifndef CAN_CLONE_SOCKETS
  close(STDOUT_FILENO);
  dup2(STDIN_FILENO, STDOUT_FILENO);
#endif
  for (;;) {
    char c;
    int l = read(STDIN_FILENO, &c, 1);
    if (l == 0) {
      fprintf(stderr, "ECHO stdin EOF\n");
      break;
    }
    if (l < 0) {
      fprintf(stderr, "ECHO error reading: %s\n", strerror(errno));
      break;
    }

    c = toupper(c);

    l = write(STDOUT_FILENO, &c, 1);
    if (l == 0) {
      fprintf(stderr, "ECHO stdout EOF\n");
      break;
    }
    if (l < 0) {
      fprintf(stderr, "ECHO error writing: %s\n", strerror(errno));
      break;
    }
  }
  fprintf(stderr, "ECHO exiting\n");
  return 0;
}

void usage(void) {
  printf("usage: passfdtest <port>\n");
  printf("       passfdtest ECHO\n");
}

int main(int argc, char** argv) {
  if (argc != 2) {
    usage();
    return -1;
  }

  if (!strcmp(argv[1], "ECHO")) {
    return echo();
  }

  return server(argv[1]);
}
