// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_PLATFORM_INTERFACE_PLATFORM_INTERFACE_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_PLATFORM_INTERFACE_PLATFORM_INTERFACE_H_

#include <grpc++/grpc++.h>

#include <sstream>
#include <string>

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
};

class PosixPlatform final : public PlatformInterface {
 public:
  ~PosixPlatform(){};
  int32_t OpenFile(std::string file_path, FileOpenMode mode);
  int32_t CloseFile(int32_t fd);
  int32_t WriteFile(int32_t fd, const char* file_contents, uint32_t write_size);
  int32_t ReadFile(int32_t fd, char* file_buf, uint32_t read_size);
  bool FileExists(std::string file_path);
  bool DirectoryExists(std::string dir_path);
  bool CreateDirectory(std::string dir_path);
  int32_t GetStubFD(uint32_t cid, uint32_t port);
  int32_t GetServerFD(uint32_t cid, uint32_t port);
  void AcceptClient(grpc::Server* server, uint32_t sockfd);
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
        get_server_fd_return_(-1){};
  ~FakePlatform(){};

  int32_t OpenFile(std::string file_path, FileOpenMode mode) { return open_file_return_; }

  int32_t CloseFile(int32_t fd) { return close_file_return_; }

  int32_t WriteFile(int32_t fd, const char* file_contents, uint32_t write_size) {
    return write_file_return_;
  }

  // The user can set the contents to be copied as part of the faked read.  If
  // the user has set a read status return, return that value instead.
  int32_t ReadFile(int32_t fd, char* file_buf, uint32_t read_size) {
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

  bool FileExists(std::string file_path) { return file_exists_return_; }

  bool DirectoryExists(std::string dir_path) { return directory_exists_return_; }

  bool CreateDirectory(std::string dir_path) { return create_directory_return_; }

  int32_t GetStubFD(uint32_t cid, uint32_t port) { return get_stub_fd_return_; }

  int32_t GetServerFD(uint32_t cid, uint32_t port) { return get_server_fd_return_; }

  void AcceptClient(grpc::Server* server, uint32_t sockfd) {}
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
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_PLATFORM_INTERFACE_PLATFORM_INTERFACE_H_
