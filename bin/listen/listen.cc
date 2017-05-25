// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <launchpad/launchpad.h>
#include <magenta/process.h>
#include <mx/job.h>
#include <mxio/io.h>
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

#include "application/lib/app/application_context.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/tasks/fd_waiter.h"
#include "lib/mtl/tasks/message_loop.h"

constexpr mx_rights_t kChildJobRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

class Service {
 public:
  Service(int port, int argc, const char** argv)
      : port_(port), argc_(argc), argv_(argv) {
    sock_ = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ < 0) {
      FTL_LOG(FATAL) << "Failed to create socket: " << strerror(errno);
    }

    struct sockaddr_in6 addr;
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port_);
    addr.sin6_addr = in6addr_any;
    if (bind(sock_, (struct sockaddr*)&addr, sizeof addr) < 0) {
      FTL_LOG(FATAL) << "Failed to bind to " << port_ << ": "
                     << strerror(errno);
    }

    if (listen(sock_, 10) < 0) {
      FTL_LOG(FATAL) << "Failed to listen:" << strerror(errno);
    }

    FTL_CHECK(mx::job::create(mx_job_default(), 0, &job_) == NO_ERROR);
    std::string job_name = ftl::StringPrintf("tcp:%d", port);
    FTL_CHECK(job_.set_property(MX_PROP_NAME, job_name.data(),
                                job_name.size()) == NO_ERROR);
    FTL_CHECK(job_.replace(kChildJobRights, &job_) == NO_ERROR);

    Wait();
  }

 private:
  void Wait() {
    waiter_.Wait(
        [this](mx_status_t success, uint32_t events) {
          int conn = accept(sock_, NULL, NULL);
          if (conn < 0) {
            FTL_LOG(FATAL) << "Failed to accept:" << strerror(errno);
          }
          Launch(conn);
          Wait();
        },
        sock_, EPOLLIN);
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

    // Transfer the socket as stdin and stdout
    launchpad_clone_fd(lp, conn, STDIN_FILENO);
    launchpad_transfer_fd(lp, conn, STDOUT_FILENO);
    // Clone this process' stderr.
    launchpad_clone_fd(lp, STDERR_FILENO, STDERR_FILENO);

    mx_handle_t proc = 0;
    const char* errmsg;

    mx_status_t status = launchpad_go(lp, &proc, &errmsg);
    if (status < 0) {
      FTL_LOG(FATAL) << "error from launchpad_go: " << errmsg;
    }
  }

  int port_;
  int argc_;
  const char** argv_;
  int sock_;
  mtl::FDWaiter waiter_;
  mx::job job_;
};

void usage(const char* command) {
  fprintf(stderr, "%s <port> <command> [<args>...]\n", command);
  exit(1);
}

int main(int argc, const char** argv) {
  mtl::MessageLoop message_loop;

  if (argc < 2) {
    usage(argv[0]);
  }

  char* end;
  int port = strtod(argv[1], &end);
  if (port == 0 || end == argv[1] || *end != '\0') {
    usage(argv[0]);
  }

  auto app_context = app::ApplicationContext::CreateFromStartupInfo();

  std::vector<std::string> command_line;
  for (int i = 2; i < argc; i++) {
    command_line.push_back(std::string(argv[i]));
  }

  Service service(port, argc - 2, argv + 2);

  message_loop.Run();
}
