// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/util.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <iostream>
#include <memory>
#include <vector>

#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_printf.h"

// Only commands & args in this whitelist may be launched (see CP-72)
const std::vector<std::vector<std::string>> kCommandWhitelist = {
    {"/system/bin/sshd", "-ire"},
};

constexpr zx_rights_t kChildJobRights =
    ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_DESTROY | ZX_RIGHT_MANAGE_JOB;

class Service {
 public:
  Service(int port, const char** argv) : port_(port), argv_(argv) {
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

    FXL_CHECK(zx::job::create(*zx::unowned<zx::job>(zx::job::default_job()), 0,
                              &job_) == ZX_OK);
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
    FXL_CHECK(zx::job::create(job_, 0, &child_job) == ZX_OK);
    FXL_CHECK(child_job.set_property(ZX_PROP_NAME, peer_name.data(),
                                     peer_name.size()) == ZX_OK);
    FXL_CHECK(child_job.replace(kChildJobRights, &child_job) == ZX_OK);

    constexpr size_t kActionCount = 3;
    fdio_spawn_action_t actions[kActionCount] = {
        // Transfer the socket as stdin and stdout
        {.action = FDIO_SPAWN_ACTION_CLONE_FD,
         .fd = {.local_fd = conn, .target_fd = STDIN_FILENO}},
        {.action = FDIO_SPAWN_ACTION_TRANSFER_FD,
         .fd = {.local_fd = conn, .target_fd = STDOUT_FILENO}},
        // Clone this process' stderr.
        {.action = FDIO_SPAWN_ACTION_CLONE_FD,
         .fd = {.local_fd = STDERR_FILENO, .target_fd = STDERR_FILENO}},
    };

    zx::process process;
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];

    zx_status_t status =
        fdio_spawn_etc(child_job.get(),
                       FDIO_SPAWN_CLONE_JOB | FDIO_SPAWN_CLONE_LDSVC |
                           FDIO_SPAWN_CLONE_NAMESPACE,
                       argv_[0], argv_, nullptr, kActionCount, actions,
                       process.reset_and_get_address(), err_msg);

    if (status < 0) {
      shutdown(conn, SHUT_RDWR);
      close(conn);
      FXL_LOG(ERROR) << "error from fdio_spawn_etc: " << err_msg;
      return;
    }

    std::unique_ptr<async::Wait> waiter =
        std::make_unique<async::Wait>(process.get(), ZX_PROCESS_TERMINATED);
    waiter->set_handler(
        [this, process = std::move(process), job = std::move(child_job)](
            async_t*, async::Wait*, zx_status_t status,
            const zx_packet_signal_t* signal) mutable {
          ProcessTerminated(std::move(process), std::move(job));
        });
    waiter->Begin(async_get_default());
    process_waiters_.push_back(std::move(waiter));
  }

  void ProcessTerminated(zx::process process, zx::job job) {
    // Kill the process and the job.
    FXL_CHECK(process.kill() == ZX_OK);
    FXL_CHECK(job.kill() == ZX_OK);

    // Find the waiter.
    auto i = std::find_if(process_waiters_.begin(), process_waiters_.end(),
                          [&process](const std::unique_ptr<async::Wait>& w) {
                            return w->object() == process.get();
                          });
    // And remove it.
    if (i != process_waiters_.end()) {
      process_waiters_.erase(i);
    }
  }

  int port_;
  const char** argv_;
  int sock_;
  fsl::FDWaiter waiter_;
  zx::job job_;

  std::vector<std::unique_ptr<async::Wait>> process_waiters_;
};

bool is_whitelisted(int argc, const char** argv) {
  std::vector<std::string> args(argv, argv + argc);
  auto it = std::find(kCommandWhitelist.begin(), kCommandWhitelist.end(), args);
  if (it == kCommandWhitelist.end()) {
    std::cerr << "Command not whitelisted: ";
    for (auto it2 = args.begin(); it2 != args.end(); it2++) {
      std::cerr << " " << *it2;
    }
    std::cerr << std::endl;
    return false;
  }
  return true;
}

void usage(const char* command) {
  std::cerr << command << " <port> <command> [<args>...]" << std::endl;
}

int main(int argc, const char** argv) {
  // We need to close PA_DIRECTORY_REQUEST otherwise clients that expect us to
  // offer services won't know that we've started and are not going to offer
  // any services.
  //
  // TODO(abarth): Instead of closing this handle, we should offer some
  // introspection services for debugging.
  zx_handle_close(zx_take_startup_handle(PA_DIRECTORY_REQUEST));

  async::Loop loop;
  async_set_default(loop.async());

  if (argc < 3) {
    usage(argv[0]);
    return 1;
  }

  char* end;
  int port = strtod(argv[1], &end);
  if (port == 0 || end == argv[1] || *end != '\0') {
    usage(argv[0]);
    return 1;
  }

  if (!is_whitelisted(argc - 2, argv + 2)) {
    return 1;
  }

  Service service(port, argv + 2);

  loop.Run();
  async_set_default(NULL);
  return 0;
}
