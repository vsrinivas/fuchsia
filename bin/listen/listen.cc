// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <launchpad/launchpad.h>
#include <magenta/process.h>
#include <mx/job.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_printf.h"


// Currently we need to forward bytes from the network socket to the subprocess.
// When we fix MG-663 this won't be needed.
#define FORWARD_BYTES


#ifdef FORWARD_BYTES

template <size_t Size>
class Buffer {
 public:
  bool Empty() { return space_start_ == 0; }
  bool Full() { return space_start_ == Size; }

  bool ReadFrom(int fd) {
    ssize_t len = read(fd, (void*)(data_ + space_start_), Size - space_start_);
    if (len < 0) {
      FTL_LOG(FATAL) << "read failed: " << strerror(errno);
    }
    if (len == 0) {
      return false;
    }
    space_start_ += len;
    return true;
  }

  bool WriteTo(int fd) {
    ssize_t len = write(fd, data_, space_start_);
    if (len < 0) {
      FTL_LOG(FATAL) << "stdin write failed: " << strerror(errno);
      return false;
    }
    space_start_ -= len;
    if (space_start_ > 0) {
      memmove(data_, data_ + len, space_start_);
    }
    return true;
  }

 private:
  // Where does free space start?
  size_t space_start_;
  // Buffer for data.
  char data_[Size];
};

void subprocess_thread(int sock, int in, int out) {
  Buffer<1024> input_buffer;
  Buffer<1024> output_buffer;

  for (;;) {
    int maxfd = -1;
    fd_set rset;
    fd_set wset;
    FD_ZERO(&rset);
    FD_ZERO(&wset);

    if (!input_buffer.Empty()) {
      // Have bytes we want to send to the process.
      FD_SET(in, &wset);
      maxfd = std::max(maxfd, in);
    }

    if (!output_buffer.Empty()) {
      // Have bytes we want to send to the network.
      FD_SET(sock, &wset);
      maxfd = std::max(maxfd, sock);
    }

    if (!input_buffer.Full()) {
      // Have more room for bytes from the network.
      FD_SET(sock, &rset);
      maxfd = std::max(maxfd, sock);
    }

    if (!output_buffer.Full()) {
      // Have more room for bytes from the process.
      FD_SET(out, &rset);
      maxfd = std::max(maxfd, out);
    }

    FTL_DCHECK(maxfd >= 0);

    int nfds = select(maxfd + 1, &rset, &wset, NULL, NULL);
    if (nfds < 0) {
      FTL_LOG(FATAL) << "select failed: " << strerror(errno);
    }

    if (FD_ISSET(sock, &rset)) {
      if (!input_buffer.ReadFrom(sock)) {
        break;
      }
    }

    if (FD_ISSET(out, &rset)) {
      if (!output_buffer.ReadFrom(out)) {
        break;
      }
    }

    if (FD_ISSET(sock, &wset)) {
      output_buffer.WriteTo(sock);
    }
    if (FD_ISSET(in, &wset)) {
      input_buffer.WriteTo(in);
    }
  }
  close(sock);
  close(in);
  close(out);
  // TODO: do we need to kill the process too?
}

#endif  // FORWARD_BYTES

class Service {
 public:
  Service(int port, int argc, const char** argv)
      : port_(port), argc_(argc), argv_(argv) {
    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ < 0) {
      FTL_LOG(FATAL) << "Failed to create socket: " << strerror(errno);
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock_, (struct sockaddr*)&addr, sizeof addr) < 0) {
      FTL_LOG(FATAL) << "Failed to bind to " << port_ << ": "
                     << strerror(errno);
    }

    if (listen(sock_, 10) < 0) {
      FTL_LOG(FATAL) << "Failed to listen:" << strerror(errno);
    }

    mx_status_t status = mx::job::create(mx_job_default(), 0, &job_);
    FTL_CHECK(status == 0);
    std::string job_name = ftl::StringPrintf("tcp:%d", port);
    status = job_.set_property(MX_PROP_NAME, job_name.data(), job_name.size());
  }

  void Launch(int conn) {
    launchpad_t* lp;
    launchpad_create(job_.get(), argv_[0], &lp);
    launchpad_load_from_file(lp, argv_[0]);
    launchpad_set_args(lp, argc_, argv_);
    // TODO: configurable cwd
    // TODO: filesystem sandboxing
    launchpad_clone(lp, LP_CLONE_MXIO_ROOT | LP_CLONE_MXIO_CWD);
    // TODO: set up environment

#ifdef FORWARD_BYTES
    int in;
    int out_pipe[2];
    if (pipe(out_pipe) < 0) {
      FTL_LOG(FATAL) << "pipe failed: " << strerror(errno);
    }
    launchpad_add_pipe(lp, &in, STDIN_FILENO);
    launchpad_clone_fd(lp, out_pipe[1], STDOUT_FILENO);
#else  // FORWARD_BYTES
    launchpad_clone_fd(lp, conn, STDIN_FILENO);
    launchpad_clone_fd(lp, conn, STDOUT_FILENO);
#endif  // FORWARD_BYTES
    launchpad_clone_fd(lp, STDERR_FILENO, STDERR_FILENO);

    mx_handle_t proc = 0;
    const char* errmsg;

    mx_status_t status = launchpad_go(lp, &proc, &errmsg);
    if (status < 0) {
      FTL_LOG(FATAL) << "error from launchpad_go: " << errmsg;
    }

#ifdef FORWARD_BYTES
    // Close our copy of their end of the output pipe.
    close(out_pipe[1]);

    std::thread subprocess(subprocess_thread, conn, in, out_pipe[0]);
    subprocess.detach();
#endif  // FORWARD_BYTES
  }

  int sock() { return sock_; }
  int port() { return port_; }

 private:
  int port_;
  int argc_;
  const char** argv_;
  int sock_;
  mx::job job_;
};

void usage(const char* command) {
  fprintf(stderr, "%s <port> <command> [<args>...]\n", command);
  exit(1);
}

int main(int argc, const char** argv) {
  if (argc < 2) {
    usage(argv[0]);
  }

  char* end;
  int port = strtod(argv[1], &end);
  if (port == 0 || end == argv[1] || *end != '\0') {
    usage(argv[0]);
  }
  std::vector<std::string> command_line;
  for (int i = 2; i < argc; i++) {
    command_line.push_back(std::string(argv[i]));
  }

  Service service(port, argc - 2, argv + 2);
  for (;;) {
    int conn = accept(service.sock(), NULL, NULL);
    if (conn < 0) {
      FTL_LOG(FATAL) << "Failed to accept:" << strerror(errno);
    }
    service.Launch(conn);
  }
}
