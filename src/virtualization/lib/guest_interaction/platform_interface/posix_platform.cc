// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wordexp.h>

#include <filesystem>

#include "platform_interface.h"
#include "src/virtualization/lib/guest_interaction/common.h"

int PosixPlatform::OpenFile(std::string file_path, FileOpenMode mode) {
  int32_t flags = O_NONBLOCK;
  if (mode == WRITE) {
    flags |= O_WRONLY | O_TRUNC | O_CREAT;
  } else {
    flags |= O_RDONLY;
  }

  int fd = open(file_path.c_str(), flags);
  if (fd < 0) {
    return -errno;
  }
  return fd;
}

ssize_t PosixPlatform::WriteFile(int fd, const char* file_contents, size_t write_size) {
  ssize_t bytes_written = 0;
  while (static_cast<size_t>(bytes_written) < write_size) {
    ssize_t curr_bytes_written =
        write(fd, file_contents + bytes_written, write_size - bytes_written);
    if (curr_bytes_written < 0) {
      return -errno;
    }

    bytes_written += curr_bytes_written;
  }
  return bytes_written;
}

ssize_t PosixPlatform::ReadFile(int fd, char* file_buf, size_t read_size) {
  ssize_t bytes_read = read(fd, file_buf, read_size);
  if (bytes_read < 0) {
    return -errno;
  }
  return bytes_read;
}

int PosixPlatform::CloseFile(int fd) {
  int close_status = close(fd);

  if (close_status < 0) {
    return -errno;
  }
  return close_status;
}

bool PosixPlatform::FileExists(std::string file_path) {
  struct stat file_stat;
  if (stat(file_path.c_str(), &file_stat) != 0) {
    return false;
  }

  return S_ISREG(file_stat.st_mode);
}

bool PosixPlatform::DirectoryExists(std::string dir_path) {
  struct stat dir_stat;
  if (stat(dir_path.c_str(), &dir_stat) != 0) {
    return false;
  }

  return S_ISDIR(dir_stat.st_mode);
}

bool PosixPlatform::CreateDirectory(std::string dir_path) {
  if (dir_path.empty()) {
    return false;
  }
  if (DirectoryExists(dir_path)) {
    return true;
  }

  char* dir_path_copy = strdup(dir_path.c_str());
  bool create_status = CreateDirectory(std::string(dirname(dir_path_copy)));
  free(dir_path_copy);

  if (!create_status) {
    return false;
  }
  if (mkdir(dir_path.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) != 0) {
    return false;
  }
  return true;
}

int PosixPlatform::GetStubFD(uint32_t cid, uint32_t port) {
  int sockfd = socket(AF_VSOCK, SOCK_STREAM | SOCK_NONBLOCK, 0);
  sockaddr_vm addr = {
      .svm_family = AF_VSOCK,
      .svm_reserved1 = 0,
      .svm_port = port,
      .svm_cid = cid,
      .svm_zero = {0},
  };
  return connect(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_vm));
}

int PosixPlatform::GetServerFD(uint32_t cid, uint32_t port) {
  int sockfd = socket(AF_VSOCK, SOCK_STREAM | SOCK_NONBLOCK, 0);
  sockaddr_vm addr = {
      .svm_family = AF_VSOCK,
      .svm_reserved1 = 0,
      .svm_port = port,
      .svm_cid = cid,
      .svm_zero = {0},
  };
  if (bind(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_vm)) != 0) {
    return errno;
  }
  if (listen(sockfd, 100) != 0) {
    return errno;
  }
  return sockfd;
}

void PosixPlatform::AcceptClient(grpc::Server* server, uint32_t sockfd) {
  sockaddr addr;
  socklen_t addr_len;
  int new_fd = accept(sockfd, &addr, &addr_len);

  if (new_fd > 0) {
    fcntl(new_fd, F_SETFL, fcntl(new_fd, F_GETFL, 0) | O_NONBLOCK);
    grpc::AddInsecureChannelFromFd(server, new_fd);
  }
}

int32_t PosixPlatform::Exec(char** args, char** env, int32_t* user_std_in, int32_t* user_std_out,
                            int32_t* user_std_err) {
  int std_in[2];
  int std_out[2];
  int std_err[2];

  if (pipe(std_in) != 0 || pipe(std_out) != 0 || pipe(std_err) != 0) {
    return -errno;
  }

  pid_t child_pid = fork();

  if (child_pid == 0) {
    if (close(std_in[1]) != 0 || close(std_out[0]) != 0 || close(std_err[0]) != 0) {
      exit(-errno);
    }

    if (dup2(std_in[0], STDIN_FILENO) < 0 || dup2(std_out[1], STDOUT_FILENO) < 0 ||
        dup2(std_err[1], STDERR_FILENO) < 0) {
      exit(-errno);
    }

    execve(args[0], &args[0], &env[0]);
    exit(-errno);
  } else if (child_pid < 0) {
    return -errno;
  } else {
    if (close(std_in[0]) != 0 || close(std_out[1]) != 0 || close(std_err[1]) != 0) {
      return -errno;
    }

    // Set read FD's to nonblocking mode.
    int flags = fcntl(std_out[0], F_GETFL, 0);
    fcntl(std_out[0], F_SETFL, flags | O_NONBLOCK);

    flags = fcntl(std_err[0], F_GETFL, 0);
    fcntl(std_err[0], F_SETFL, flags | O_NONBLOCK);

    *user_std_in = std_in[1];
    *user_std_out = std_out[0];
    *user_std_err = std_err[0];

    return child_pid;
  }
}

int32_t PosixPlatform::WaitPid(int32_t pid, int32_t* status, int32_t flags) {
  int32_t poll_pid = waitpid(pid, status, flags);
  if (poll_pid < 0) {
    return -errno;
  }
  return poll_pid;
}

int PosixPlatform::KillPid(int32_t pid, int32_t signal) {
  int ret = kill(pid, signal);

  if (ret < 0) {
    return -errno;
  }
  return ret;
}

void PosixPlatform::SetFileNonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

std::vector<std::string> PosixPlatform::ParseCommand(std::string command) {
  wordexp_t command_line;
  int32_t parse_result = wordexp(command.c_str(), &command_line, 0);

  std::vector<std::string> argv;
  if (parse_result == 0) {
    for (uint32_t i = 0; i < command_line.we_wordc; i++) {
      argv.push_back(command_line.we_wordv[i]);
    }
  }

  wordfree(&command_line);

  return argv;
}
