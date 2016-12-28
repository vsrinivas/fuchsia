// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a TCP service that accepts shell commands, runs them, and streams
// back output to the socket. The protocol is as follows:
// - Client connects, sends a single line representing the shell command to run.
// - Server launches the shell command, reads in the stdout and stderr of the child
//   process, and flushes each line to the client.
// - Once the process has shut down, server sends client a new line, followed by
//   ASCII-encoded signed integer representing the process exit code.

// TODO(vardhan): Process multiple clients/commands at the same time.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <functional>
#include <string>
#include <vector>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/processargs.h>
#include <magenta/types.h>
#include <mx/process.h>
#include <mxio/util.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_view.h"

namespace {

// TODO(vardhan): Make listen port command-line configurable.
constexpr uint16_t kListenPort = 8342;  // TCP port
constexpr uint16_t kMaxCommandLength = 2048;  // in bytes.
constexpr uint16_t kMaxStdReadBytes = 1024;  // read threshold for stdout/err.

// Represents a client connection. Self-owned.
class TestRunnerConnection {
 public:
  explicit TestRunnerConnection(int socket_fd)
    : socket_(socket_fd) {};

  void Start() {
    ReadCommand();
  }

 private:
  ~TestRunnerConnection() {
    close(socket_);
    if (lp_) {
      launchpad_destroy(lp_);
    }
  }

  // Read an entire line representing the command to run. Blocks until we have a
  // line. Fails if we hit |kMaxCommandLength| chars. Deletes this object once
  // the command is executed.
  void ReadCommand() {
    char buf[kMaxCommandLength];
    size_t read_so_far = 0;
    while (read_so_far < kMaxCommandLength) {
      ssize_t n = read(socket_, buf + read_so_far, sizeof(buf) - read_so_far);
      FTL_CHECK(n > 0);
      read_so_far += n;
      // Is there a line?
      if (static_cast<char*>(memchr(buf, '\n', read_so_far)) != NULL) {
        break;
      }
    }
    if (read_so_far < kMaxCommandLength) {
      RunCommand(ftl::StringView(buf, read_so_far));
    }
    delete this;
  }

  // This mostly resembles launchpad_launch_mxio*(), with a modification: we
  // control the reading-side of stdout/err.
  void LaunchProcess(
      const std::vector<const char*>& args) {
    mx_handle_t job_to_child = MX_HANDLE_INVALID;
    mx_handle_t job = mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_JOB, 0));
    if (job > 0)
      mx_handle_duplicate(job, MX_RIGHT_SAME_RIGHTS, &job_to_child);

    mx_handle_t vmo = launchpad_vmo_from_file(args[0]);
    mx_status_t status = launchpad_create(job_to_child, args[0], &lp_);
    if (status == NO_ERROR) {
      status = launchpad_elf_load(lp_, vmo);
      if (status == NO_ERROR)
        status = launchpad_load_vdso(lp_, MX_HANDLE_INVALID);
      if (status == NO_ERROR)
        status = launchpad_add_vdso_vmo(lp_);
      if (status == NO_ERROR)
        status = launchpad_arguments(lp_, args.size(), args.data());
      if (status == NO_ERROR)
        status = launchpad_environ(lp_, environ);
      if (status == NO_ERROR)
        status = launchpad_clone_mxio_root(lp_);
      if (status == NO_ERROR)
        status = launchpad_clone_mxio_cwd(lp_);
      if (status == NO_ERROR)
        status = launchpad_add_handles(lp_, 0, nullptr, nullptr);
      if (status == NO_ERROR)
        status = launchpad_clone_fd(lp_, STDIN_FILENO, STDIN_FILENO);
      if (status == NO_ERROR)
        status = launchpad_add_pipe(lp_, &fd_stdout_, STDOUT_FILENO);
      if (status == NO_ERROR)
        status = launchpad_add_pipe(lp_, &fd_stderr_, STDERR_FILENO);
    } else {
      FTL_LOG(ERROR) << "Could not prepare launch for: " << args[0];
      mx_handle_close(vmo);
      return;
    }

    if (status == NO_ERROR) {
      process_ = mx::process(launchpad_start(lp_));
    } else {
      FTL_LOG(INFO) << "Could not prepare launch for: " << args[0];
      process_ = mx::process(status);
    }
  }

  // This blocks. Reads stdout/err from child process, writes them line-by-line
  // to the socket.
  void DrainOutput() {
    int epollfd = epoll_create1(0);
    FTL_CHECK(epollfd != -1);

    struct pipe_description pipe_desc[2] = {
      {fd_stdout_, std::string()},
      {fd_stderr_, std::string()}
    };
    struct epoll_event events[2] = {
      {EPOLLIN, {&pipe_desc[0]}},
      {EPOLLIN, {&pipe_desc[1]}},
    };

    pipe_desc[0].buffer.reserve(kMaxStdReadBytes);
    pipe_desc[1].buffer.reserve(kMaxStdReadBytes);

    FTL_CHECK(epoll_ctl(epollfd, EPOLL_CTL_ADD, pipe_desc[0].fd,
        &events[0]) != -1);
    FTL_CHECK(epoll_ctl(epollfd, EPOLL_CTL_ADD, pipe_desc[1].fd,
        &events[1]) != -1);

    int opened_pipes = 2;
    while (opened_pipes) {
      int n = epoll_wait(epollfd, events, 2, -1);
      FTL_CHECK(n != -1);
      for (int i = 0; i < n; i++) {
        struct pipe_description* event_data =
            static_cast<struct pipe_description*>(events[i].data.ptr);
        if (!ReadInto(event_data->fd, &event_data->buffer)) {
          FTL_CHECK(epoll_ctl(epollfd, EPOLL_CTL_DEL, event_data->fd,
            nullptr) != -1);
          opened_pipes--;
          continue;
        }

        FlushLinesToSocket(&event_data->buffer);
      }
    }

    close(epollfd);
  }

  // Writes available lines from |in_buf| into the socket.
  void FlushLinesToSocket(std::string* in_buf) {
    auto total_write = in_buf->rfind('\n');
    if (total_write == std::string::npos) {
      return;
    }
    total_write++;

    if (total_write > 0) {
      FTL_CHECK(write(socket_, in_buf->data(), total_write)
          == static_cast<ssize_t>(total_write));
      in_buf->erase(0, total_write);
    }
  }

  // Returns false if |in_fd| is closed. Reads from |in_fd| into |buffer|. Reads
  // up to |kMaxStdReadBytes| bytes worth.
  bool ReadInto(int in_fd, std::string* buffer) {
    char buf[kMaxStdReadBytes];
    ssize_t bytes_read = read(in_fd, buf, sizeof(buf));
    if (bytes_read == 0 || (bytes_read == -1 && errno == ENOTCONN))
      return false;

    FTL_CHECK(bytes_read > 0)
      << "bytes_read = " << bytes_read
      << ", errno = " << errno;
    buffer->append(buf, bytes_read);
    return true;
  }

  // Blocks until the child process is terminated, and returns its exit code.
  int WaitForTermination() {
    FTL_CHECK(process_.wait_one(MX_TASK_TERMINATED,
        MX_TIME_INFINITE, nullptr) == NO_ERROR);

    mx_info_process_t proc_info;
    FTL_CHECK(mx_object_get_info(process_.get(), MX_INFO_PROCESS, &proc_info, sizeof(proc_info),
        nullptr, nullptr) == NO_ERROR);

    return proc_info.return_code;
  }

  // This will block until both stdout and stderr of the |command| are closed.
  void RunCommand(ftl::StringView command) {
    auto cmd_str = command.ToString();
    std::vector<const char*> args = {
      "/boot/bin/sh",
      "-c",
      cmd_str.c_str(),
    };
    LaunchProcess(args);
    DrainOutput();
    int exit_code = WaitForTermination();

    // Always end with an ASCII-encoded exit code on a new line.
    std::stringstream epilogue;
    epilogue << "\n" << exit_code;
    std::string bytes = epilogue.str();
    FTL_CHECK(write(socket_, bytes.data(), bytes.size()) > 0);
  }

  struct pipe_description {
    int fd;
    std::string buffer;
  };

  launchpad_t* lp_{};
  mx::process process_;

  int socket_;
  int fd_stdout_;
  int fd_stderr_;
};

class TestRunnerService {
 public:
  TestRunnerService(uint16_t port) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 1. Make a TCP socket.
    listener_ = socket(addr.sin_family, SOCK_STREAM, IPPROTO_TCP);
    FTL_CHECK(listener_ != -1);

    // 2. Bind it to an address.
    FTL_CHECK(bind(listener_,
                   reinterpret_cast<struct sockaddr*>(&addr),
                   sizeof(addr)) != -1);

    // 3. Make it a listening socket.
    FTL_CHECK(listen(listener_, 100) != -1);
  }

  ~TestRunnerService() { close(listener_); }

  TestRunnerConnection* AcceptConnection() {
    int sockfd = accept(listener_, nullptr, nullptr);
    if (sockfd == -1) {
      FTL_LOG(INFO) << "accept() oops";
    }
    return new TestRunnerConnection(sockfd);
  }

 private:
  int listener_;
};

}  // namespace

int main() {
  TestRunnerService server(kListenPort);
  while (1) {
    TestRunnerConnection* runner = server.AcceptConnection();
    // Start() blocks until the connection is closed. |runner| will delete
    // itself when it is done.
    runner->Start();
  }
  return 0;
}
