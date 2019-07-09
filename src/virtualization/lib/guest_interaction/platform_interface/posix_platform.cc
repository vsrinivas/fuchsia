// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>

#include "platform_interface.h"

int32_t PosixPlatform::OpenFile(std::string file_path, FileOpenMode mode) {
  int32_t flags = O_NONBLOCK;
  if (mode == WRITE) {
    flags |= O_WRONLY | O_TRUNC | O_CREAT;
  } else {
    flags |= O_RDONLY;
  }

  int32_t fd = open(file_path.c_str(), flags);
  if (fd < 0) {
    return -errno;
  }
  return fd;
}

int32_t PosixPlatform::WriteFile(int32_t fd, const char* file_contents, uint32_t write_size) {
  ssize_t bytes_written = 0;

  while (bytes_written < write_size) {
    ssize_t curr_bytes_written =
        write(fd, file_contents + bytes_written, write_size - bytes_written);
    if (curr_bytes_written < 0) {
      return -errno;
    }

    bytes_written += curr_bytes_written;
  }
  return bytes_written;
}

int32_t PosixPlatform::ReadFile(int32_t fd, char* file_buf, uint32_t read_size) {
  ssize_t bytes_read = read(fd, file_buf, read_size);
  if (bytes_read < 0) {
    return -errno;
  }
  return bytes_read;
}

int32_t PosixPlatform::CloseFile(int32_t fd) {
  int32_t close_status = close(fd);

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
  if (dir_path.size() == 0) {
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

int32_t PosixPlatform::GetStubFD(uint32_t cid, uint32_t port) {
  int sockfd = socket(AF_VSOCK, SOCK_STREAM | SOCK_NONBLOCK, 0);
  sockaddr_vm addr = {
      .svm_family = AF_VSOCK,
      .svm_reserved1 = 0,
      .svm_port = port,
      .svm_cid = cid,
      .svm_zero = {0},
  };
  return connect(sockfd, (sockaddr*)&addr, sizeof(sockaddr_vm));
}

int32_t PosixPlatform::GetServerFD(uint32_t cid, uint32_t port) {
  int sockfd = socket(AF_VSOCK, SOCK_STREAM | SOCK_NONBLOCK, 0);
  sockaddr_vm addr = {
      .svm_family = AF_VSOCK,
      .svm_reserved1 = 0,
      .svm_port = port,
      .svm_cid = cid,
      .svm_zero = {0},
  };
  if (bind(sockfd, (sockaddr*)&addr, sizeof(sockaddr_vm)) != 0) {
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
