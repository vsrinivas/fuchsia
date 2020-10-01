// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_PLATFORM_INTERFACE_PLATFORM_INTERFACE_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_PLATFORM_INTERFACE_PLATFORM_INTERFACE_H_

#include <sstream>
#include <string>
#include <vector>

#include <grpc++/grpc++.h>

enum FileOpenMode { READ, WRITE };

class PlatformInterface {
 public:
  ~PlatformInterface() = default;
  virtual int OpenFile(std::string file_path, FileOpenMode mode) = 0;
  virtual int CloseFile(int fd) = 0;
  virtual ssize_t WriteFile(int fd, const char* file_contents, size_t write_size) = 0;
  virtual ssize_t ReadFile(int fd, char* file_buf, size_t read_size) = 0;
  virtual bool FileExists(std::string file_path) = 0;
  virtual bool DirectoryExists(std::string dir_path) = 0;
  virtual bool CreateDirectory(std::string dir_path) = 0;
  virtual int GetStubFD(uint32_t cid, uint32_t port) = 0;
  virtual int GetServerFD(uint32_t cid, uint32_t port) = 0;
  virtual void AcceptClient(grpc::Server* server, uint32_t sockfd) = 0;
  virtual int32_t Exec(char** args, char** env, int32_t* std_in, int32_t* std_out,
                       int32_t* std_err) = 0;
  virtual int32_t WaitPid(int32_t pid, int32_t* status, int32_t flags) = 0;
  virtual int KillPid(int32_t pid, int32_t signal) = 0;
  virtual void SetFileNonblocking(int fd) = 0;
  virtual std::vector<std::string> ParseCommand(std::string command) = 0;
};

class PosixPlatform final : public PlatformInterface {
 public:
  ~PosixPlatform() = default;
  int OpenFile(std::string file_path, FileOpenMode mode) override;
  int CloseFile(int fd) override;
  ssize_t WriteFile(int fd, const char* file_contents, size_t write_size) override;
  ssize_t ReadFile(int fd, char* file_buf, size_t read_size) override;
  bool FileExists(std::string file_path) override;
  bool DirectoryExists(std::string dir_path) override;
  bool CreateDirectory(std::string dir_path) override;
  int GetStubFD(uint32_t cid, uint32_t port) override;
  int GetServerFD(uint32_t cid, uint32_t port) override;
  void AcceptClient(grpc::Server* server, uint32_t sockfd) override;
  int32_t Exec(char** args, char** env, int32_t* std_in, int32_t* std_out,
               int32_t* std_err) override;
  int32_t WaitPid(int32_t pid, int32_t* status, int32_t flags) override;
  int KillPid(int32_t pid, int32_t signal) override;
  void SetFileNonblocking(int fd) override;
  std::vector<std::string> ParseCommand(std::string command) override;
};

class FakePlatform final : public PlatformInterface {
 public:
  FakePlatform() = default;
  ~FakePlatform() = default;

  int OpenFile(std::string file_path, FileOpenMode mode) override { return open_file_return_; }

  int CloseFile(int32_t fd) override { return close_file_return_; }

  ssize_t WriteFile(int32_t fd, const char* file_contents, size_t write_size) override {
    return write_file_return_;
  }

  // The user can set the contents to be copied as part of the faked read.  If
  // the user has set a read status return, return that value instead.
  ssize_t ReadFile(int32_t fd, char* file_buf, size_t read_size) override {
    if (read_file_return_ < 0) {
      return read_file_return_;
    }

    // If the string is empty, return 0 effectively indicating EOF.
    if (read_file_contents_.empty()) {
      return 0;
    }

    strcpy(file_buf, read_file_contents_.c_str());
    return read_file_contents_.size();
  }

  bool FileExists(std::string file_path) override { return file_exists_return_; }

  bool DirectoryExists(std::string dir_path) override { return directory_exists_return_; }

  bool CreateDirectory(std::string dir_path) override { return create_directory_return_; }

  int GetStubFD(uint32_t cid, uint32_t port) override { return get_stub_fd_return_; }

  int GetServerFD(uint32_t cid, uint32_t port) override { return get_server_fd_return_; }

  void AcceptClient(grpc::Server* server, uint32_t sockfd) override {}

  int32_t Exec(char** args, char** env, int32_t* std_in, int32_t* std_out,
               int32_t* std_err) override {
    return exec_return_;
  }

  int32_t WaitPid(int32_t pid, int32_t* status, int32_t flags) override { return waitpid_return_; }

  int32_t KillPid(int32_t pid, int32_t signal) override { return kill_pid_return_; }

  void SetFileNonblocking(int32_t fd) override {}

  std::vector<std::string> ParseCommand(std::string command) override {
    return parse_command_return_;
  }

  void SetOpenFileReturn(int value) { open_file_return_ = value; }
  void SetWriteFileReturn(ssize_t value) { write_file_return_ = value; }
  void SetReadFileContents(std::string value) { read_file_contents_ = std::move(value); }
  void SetReadFileReturn(ssize_t value) { read_file_return_ = value; }
  void SetCloseFileReturn(int value) { close_file_return_ = value; }
  void SetFileExistsReturn(bool value) { file_exists_return_ = value; }
  void SetDirectoryExistsReturn(bool value) { directory_exists_return_ = value; }
  void SetCreateDirectoryReturn(bool value) { create_directory_return_ = value; }
  void SetGetStubFDReturn(int value) { get_stub_fd_return_ = value; }
  void SetGetServerFDReturn(int value) { get_server_fd_return_ = value; }
  void SetExecReturn(int32_t value) { exec_return_ = value; }
  void SetWaitPidReturn(int32_t value) { waitpid_return_ = value; }
  void SetKillPidReturn(int value) { kill_pid_return_ = value; }
  void SetParseCommandReturn(std::vector<std::string> value) {
    parse_command_return_ = std::move(value);
  }

 private:
  int open_file_return_ = -1;
  ssize_t write_file_return_ = -1;
  std::string read_file_contents_;
  ssize_t read_file_return_ = 0;
  int close_file_return_ = -1;
  bool file_exists_return_ = false;
  bool directory_exists_return_ = false;
  bool create_directory_return_ = false;
  int get_stub_fd_return_ = -1;
  int get_server_fd_return_ = -1;
  int32_t exec_return_ = -1;
  int32_t waitpid_return_ = -1;
  int kill_pid_return_ = -1;
  std::vector<std::string> parse_command_return_;
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_PLATFORM_INTERFACE_PLATFORM_INTERFACE_H_
