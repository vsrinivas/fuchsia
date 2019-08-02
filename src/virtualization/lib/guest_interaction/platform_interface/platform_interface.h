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
  ~PlatformInterface(){};
  virtual int32_t OpenFile(std::string file_path, FileOpenMode mode) = 0;
  virtual int32_t CloseFile(int32_t fd) = 0;
  virtual int32_t WriteFile(int32_t fd, const char* file_contents, uint32_t write_size) = 0;
  virtual int32_t ReadFile(int32_t fd, char* file_buf, uint32_t read_size) = 0;
  virtual bool FileExists(std::string file_path) = 0;
  virtual bool DirectoryExists(std::string dir_path) = 0;
  virtual bool CreateDirectory(std::string dir_path) = 0;
  virtual int32_t GetStubFD(uint32_t cid, uint32_t port) = 0;
  virtual int32_t GetServerFD(uint32_t cid, uint32_t port) = 0;
  virtual void AcceptClient(grpc::Server* server, uint32_t sockfd) = 0;
  virtual int32_t Exec(char** args, char** env, int32_t* std_in, int32_t* std_out,
                       int32_t* std_err) = 0;
  virtual int32_t WaitPid(int32_t pid, int32_t* status, int32_t flags) = 0;
  virtual int32_t KillPid(int32_t pid, int32_t signal) = 0;
  virtual void SetFileNonblocking(int32_t fd) = 0;
  virtual std::vector<std::string> ParseCommand(std::string command) = 0;
};

class PosixPlatform final : public PlatformInterface {
 public:
  ~PosixPlatform(){};
  int32_t OpenFile(std::string file_path, FileOpenMode mode) override;
  int32_t CloseFile(int32_t fd) override;
  int32_t WriteFile(int32_t fd, const char* file_contents, uint32_t write_size) override;
  int32_t ReadFile(int32_t fd, char* file_buf, uint32_t read_size) override;
  bool FileExists(std::string file_path) override;
  bool DirectoryExists(std::string dir_path) override;
  bool CreateDirectory(std::string dir_path) override;
  int32_t GetStubFD(uint32_t cid, uint32_t port) override;
  int32_t GetServerFD(uint32_t cid, uint32_t port) override;
  void AcceptClient(grpc::Server* server, uint32_t sockfd) override;
  int32_t Exec(char** args, char** env, int32_t* std_in, int32_t* std_out,
               int32_t* std_err) override;
  int32_t WaitPid(int32_t pid, int32_t* status, int32_t flags) override;
  int32_t KillPid(int32_t pid, int32_t signal) override;
  void SetFileNonblocking(int32_t fd) override;
  std::vector<std::string> ParseCommand(std::string command) override;
};

class FakePlatform final : public PlatformInterface {
 public:
  FakePlatform()
      : open_file_return_(-1),
        write_file_return_(-1),
        read_file_contents_(""),
        read_file_return_(0),
        close_file_return_(-1),
        file_exists_return_(false),
        directory_exists_return_(false),
        create_directory_return_(false),
        get_stub_fd_return_(-1),
        get_server_fd_return_(-1),
        exec_return_(-1),
        waitpid_return_(-1),
        kill_pid_return_(-1),
        parse_command_return_({}){};
  ~FakePlatform(){};

  int32_t OpenFile(std::string file_path, FileOpenMode mode) override { return open_file_return_; }

  int32_t CloseFile(int32_t fd) override { return close_file_return_; }

  int32_t WriteFile(int32_t fd, const char* file_contents, uint32_t write_size) override {
    return write_file_return_;
  }

  // The user can set the contents to be copied as part of the faked read.  If
  // the user has set a read status return, return that value instead.
  int32_t ReadFile(int32_t fd, char* file_buf, uint32_t read_size) override {
    if (read_file_return_ < 0) {
      return read_file_return_;
    }

    // If the string is empty, return 0 effectively indicating EOF.
    if (read_file_contents_.size() == 0) {
      return 0;
    }

    strcpy(file_buf, read_file_contents_.c_str());
    return read_file_contents_.size();
  }

  bool FileExists(std::string file_path) override { return file_exists_return_; }

  bool DirectoryExists(std::string dir_path) override { return directory_exists_return_; }

  bool CreateDirectory(std::string dir_path) override { return create_directory_return_; }

  int32_t GetStubFD(uint32_t cid, uint32_t port) override { return get_stub_fd_return_; }

  int32_t GetServerFD(uint32_t cid, uint32_t port) override { return get_server_fd_return_; }

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

  void SetOpenFileReturn(int32_t value) { open_file_return_ = value; }
  void SetWriteFileReturn(int32_t value) { write_file_return_ = value; }
  void SetReadFileContents(std::string value) { read_file_contents_ = value; }
  void SetReadFileReturn(int32_t value) { read_file_return_ = value; }
  void SetCloseFileReturn(int32_t value) { close_file_return_ = value; }
  void SetFileExistsReturn(bool value) { file_exists_return_ = value; }
  void SetDirectoryExistsReturn(bool value) { directory_exists_return_ = value; }
  void SetCreateDirectoryReturn(bool value) { create_directory_return_ = value; }
  void SetGetStubFDReturn(int32_t value) { get_stub_fd_return_ = value; }
  void SetGetServerFDReturn(int32_t value) { get_server_fd_return_ = value; }
  void SetExecReturn(int32_t value) { exec_return_ = value; }
  void SetWaitPidReturn(int32_t value) { waitpid_return_ = value; }
  void SetKillPidReturn(int32_t value) { kill_pid_return_ = value; }
  void SetParseCommandReturn(std::vector<std::string> value) {
    parse_command_return_ = std::move(value);
  }

 private:
  int32_t open_file_return_;
  int32_t write_file_return_;
  std::string read_file_contents_;
  int32_t read_file_return_;
  int32_t close_file_return_;
  bool file_exists_return_;
  bool directory_exists_return_;
  bool create_directory_return_;
  int32_t get_stub_fd_return_;
  int32_t get_server_fd_return_;
  int32_t exec_return_;
  int32_t waitpid_return_;
  int32_t kill_pid_return_;
  std::vector<std::string> parse_command_return_;
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_PLATFORM_INTERFACE_PLATFORM_INTERFACE_H_
