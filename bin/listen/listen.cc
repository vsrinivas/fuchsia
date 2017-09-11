// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <launchpad/launchpad.h>
#include <magenta/process.h>
#include <mx/job.h>
#include <mxio/io.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/mtl/tasks/fd_waiter.h"
#include "lib/mtl/tasks/message_loop.h"

constexpr mx_rights_t kChildJobRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

class Service : private mtl::MessageLoopHandler {
 public:
  Service(int port, int argc, const char** argv)
      : port_(port), argc_(argc), argv_(argv) {
    sock_ = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ < 0) {
      FXL_LOG(ERROR) << "Failed to create socket: " << strerror(errno);
      exit(1);
    }

    struct sockaddr_in6 addr;
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port_);
    addr.sin6_addr = in6addr_any;
    if (bind(sock_, (struct sockaddr*)&addr, sizeof addr) < 0) {
      FXL_LOG(ERROR) << "Failed to bind to " << port_ << ": "
                     << strerror(errno);
      exit(1);
    }

    if (listen(sock_, 10) < 0) {
      FXL_LOG(ERROR) << "Failed to listen: " << strerror(errno);
      exit(1);
    }

    FXL_CHECK(mx::job::create(mx_job_default(), 0, &job_) == MX_OK);
    std::string job_name = fxl::StringPrintf("tcp:%d", port);
    FXL_CHECK(job_.set_property(MX_PROP_NAME, job_name.data(),
                                job_name.size()) == MX_OK);
    FXL_CHECK(job_.replace(kChildJobRights, &job_) == MX_OK);

    Wait();
  }

  ~Service() {
    for (auto iter = process_handler_key_.begin(); iter != process_handler_key_.end(); iter++) {
      process_handler_key_.erase(iter);
      mtl::MessageLoop::GetCurrent()->RemoveHandler(iter->second);
      FXL_CHECK(mx_task_kill(iter->first) == MX_OK);
      FXL_CHECK(mx_handle_close(iter->first) == MX_OK);
    }
  }

 private:
  void Wait() {
    waiter_.Wait(
        [this](mx_status_t success, uint32_t events) {
          struct sockaddr_in6 peer_addr;
          socklen_t peer_addr_len = sizeof(peer_addr);
          int conn = accept(sock_, (struct sockaddr*)&peer_addr, &peer_addr_len);
          if (conn < 0) {
            if (errno == EPIPE) {
              FXL_LOG(ERROR) << "The netstack died. Terminating.";
              exit(1);
            } else {
              FXL_LOG(ERROR) << "Failed to accept: " << strerror(errno);
              // Wait for another connection.
              Wait();
            }
            return;
          }
          std::string peer_name = "unknown";
          char host[32];
          char port[16];
          if (getnameinfo((struct sockaddr*)&peer_addr, peer_addr_len, host,
                          sizeof(host), port, sizeof(port),
                          NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            peer_name = fxl::StringPrintf("%s:%s", host, port);
          }
          Launch(conn, peer_name);
          Wait();
        },
        sock_, POLLIN);
  }

  void Launch(int conn, const std::string& peer_name) {
    // Create a new job to run the child in.
    mx::job child_job;
    FXL_CHECK(mx::job::create(job_.get(), 0, &child_job) == MX_OK);
    FXL_CHECK(child_job.set_property(MX_PROP_NAME, peer_name.data(),
                                peer_name.size()) == MX_OK);
    FXL_CHECK(child_job.replace(kChildJobRights, &child_job) == MX_OK);

    launchpad_t* lp;
    launchpad_create(child_job.get(), argv_[0], &lp);
    launchpad_load_from_file(lp, argv_[0]);
    launchpad_set_args(lp, argc_, argv_);
    // TODO: configurable cwd
    // TODO: filesystem sandboxing
    launchpad_clone(lp, LP_CLONE_MXIO_NAMESPACE | LP_CLONE_MXIO_CWD);
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
      shutdown(conn, SHUT_RDWR);
      close(conn);
      FXL_LOG(ERROR) << "error from launchpad_go: " << errmsg;
      return;
    }

    auto handler_key = mtl::MessageLoop::GetCurrent()->AddHandler(
        this, proc, MX_PROCESS_TERMINATED);
    FXL_CHECK(handler_key != 0);
    process_handler_key_.insert(std::make_pair(proc, handler_key));
  }

  // mtl::MessageLoopHandler
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending, uint64_t count) {
    FXL_CHECK(pending & MX_PROCESS_TERMINATED);
    auto iter = process_handler_key_.find(handle);
    FXL_CHECK(iter != process_handler_key_.end());
    process_handler_key_.erase(iter);
    mtl::MessageLoop::GetCurrent()->RemoveHandler(iter->second);
    FXL_CHECK(mx_task_kill(handle) == MX_OK);
    FXL_CHECK(mx_handle_close(handle) == MX_OK);
  }

  int port_;
  int argc_;
  const char** argv_;
  int sock_;
  mtl::FDWaiter waiter_;
  mx::job job_;
  std::map<mx_handle_t, mtl::MessageLoop::HandlerKey> process_handler_key_;
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
