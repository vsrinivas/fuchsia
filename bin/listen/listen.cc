// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <async/cpp/auto_wait.h>
#include <async/cpp/loop.h>
#include <async/default.h>
#include <errno.h>
#include <fdio/io.h>
#include <fdio/util.h>
#include <launchpad/launchpad.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zx/job.h>
#include <zx/process.h>

#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_printf.h"

constexpr zx_rights_t kChildJobRights =
    ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_DESTROY;

class Service {
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

    FXL_CHECK(zx::job::create(zx_job_default(), 0, &job_) == ZX_OK);
    std::string job_name = fxl::StringPrintf("tcp:%d", port);
    FXL_CHECK(job_.set_property(ZX_PROP_NAME, job_name.data(),
                                job_name.size()) == ZX_OK);
    FXL_CHECK(job_.replace(kChildJobRights, &job_) == ZX_OK);

    Wait();
  }

  ~Service() {
    for (auto iter = process_waiters_.begin(); iter != process_waiters_.end();
         iter++) {
      FXL_CHECK(zx_task_kill(iter->get()->object()) == ZX_OK);
      FXL_CHECK(zx_handle_close(iter->get()->object()) == ZX_OK);
    }
  }

 private:
  void Wait() {
    waiter_.Wait(
        [this](zx_status_t success, uint32_t events) {
          struct sockaddr_in6 peer_addr;
          socklen_t peer_addr_len = sizeof(peer_addr);
          int conn =
              accept(sock_, (struct sockaddr*)&peer_addr, &peer_addr_len);
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
    zx::job child_job;
    FXL_CHECK(zx::job::create(job_.get(), 0, &child_job) == ZX_OK);
    FXL_CHECK(child_job.set_property(ZX_PROP_NAME, peer_name.data(),
                                     peer_name.size()) == ZX_OK);
    FXL_CHECK(child_job.replace(kChildJobRights, &child_job) == ZX_OK);

    launchpad_t* lp;
    launchpad_create(child_job.get(), argv_[0], &lp);
    launchpad_load_from_file(lp, argv_[0]);
    launchpad_set_args(lp, argc_, argv_);
    // TODO: configurable cwd
    // TODO: filesystem sandboxing
    launchpad_clone(lp, LP_CLONE_FDIO_NAMESPACE);
    // TODO: set up environment

    // Transfer the socket as stdin and stdout
    launchpad_clone_fd(lp, conn, STDIN_FILENO);
    launchpad_transfer_fd(lp, conn, STDOUT_FILENO);
    // Clone this process' stderr.
    launchpad_clone_fd(lp, STDERR_FILENO, STDERR_FILENO);

    zx::process process;
    const char* errmsg;

    zx_status_t status =
        launchpad_go(lp, process.reset_and_get_address(), &errmsg);
    if (status < 0) {
      shutdown(conn, SHUT_RDWR);
      close(conn);
      FXL_LOG(ERROR) << "error from launchpad_go: " << errmsg;
      return;
    }

    std::unique_ptr<async::AutoWait> waiter = std::make_unique<async::AutoWait>(
        async_get_default(), process.get(), ZX_PROCESS_TERMINATED);
    waiter->set_handler([
      this, process = std::move(process), job = std::move(child_job)
    ](async_t*, zx_status_t status, const zx_packet_signal_t* signal) mutable {
      ProcessTerminated(std::move(process), std::move(job));
      return ASYNC_WAIT_FINISHED;
    });
    waiter->Begin();
    process_waiters_.push_back(std::move(waiter));
  }

  void ProcessTerminated(zx::process process, zx::job job) {
    // Kill the process and the job.
    FXL_CHECK(process.kill() == ZX_OK);
    FXL_CHECK(job.kill() == ZX_OK);

    // Find the waiter.
    auto i =
        std::find_if(process_waiters_.begin(), process_waiters_.end(),
                     [&process](const std::unique_ptr<async::AutoWait>& w) {
                       return w->object() == process.get();
                     });
    // And remove it.
    if (i != process_waiters_.end()) {
      process_waiters_.erase(i);
    }
  }

  int port_;
  int argc_;
  const char** argv_;
  int sock_;
  fsl::FDWaiter waiter_;
  zx::job job_;

  std::vector<std::unique_ptr<async::AutoWait>> process_waiters_;
};

void usage(const char* command) {
  fprintf(stderr, "%s <port> <command> [<args>...]\n", command);
  exit(1);
}

int main(int argc, const char** argv) {
  // We need to close PA_SERVICE_REQUEST otherwise clients that expect us to
  // offer services won't know that we've started and are not going to offer
  // any services.
  //
  // TODO(abarth): Instead of closing this handle, we should offer some
  // introspection services for debugging.
  zx_handle_close(zx_get_startup_handle(PA_SERVICE_REQUEST));

  async::Loop loop;
  async_set_default(loop.async());

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

  loop.Run();
  async_set_default(NULL);
  return 0;
}
