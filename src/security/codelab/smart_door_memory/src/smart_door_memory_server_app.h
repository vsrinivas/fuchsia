// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This is a fake 'smart door memory' component for security codelab.
// It CONTAINS vulnerability intentionally.
// DO NOT COPY ANY OF THE CODE IN THIS FILE!
#ifndef SRC_SECURITY_CODELAB_SMART_DOOR_MEMORY_SRC_SMART_DOOR_MEMORY_SERVER_APP_H_
#define SRC_SECURITY_CODELAB_SMART_DOOR_MEMORY_SRC_SMART_DOOR_MEMORY_SERVER_APP_H_

#include <fuchsia/security/codelabsmartdoor/cpp/fidl.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <unistd.h>

namespace smart_door_memory {

class SmartDoorMemoryWriter : public fuchsia::security::codelabsmartdoor::Writer {
 public:
  SmartDoorMemoryWriter(int fd, std::string file_path) : fd_(fd), file_path_(file_path) {}

  virtual void Write(std::vector<uint8_t> data, WriteCallback callback);

  ~SmartDoorMemoryWriter() { close(fd_); }

 private:
  int fd_;
  std::string file_path_;
};

class SmartDoorMemoryReader : public fuchsia::security::codelabsmartdoor::Reader {
 public:
  SmartDoorMemoryReader(int fd, std::string file_path) : fd_(fd), file_path_(file_path) {}

  virtual void Read(ReadCallback callback);

  ~SmartDoorMemoryReader() { close(fd_); }

 private:
  int fd_;
  std::string file_path_;
};

class SmartDoorMemoryServer : public fuchsia::security::codelabsmartdoor::Memory,
                              public fuchsia::security::codelabsmartdoor::MemoryReset {
 public:
  SmartDoorMemoryServer();

  static bool Log(std::string file_path, bool read_or_write);

  virtual void GenerateToken(GenerateTokenCallback callback);
  virtual void GetReader(
      fuchsia::security::codelabsmartdoor::Token token,
      ::fidl::InterfaceRequest<fuchsia::security::codelabsmartdoor::Reader> request,
      GetReaderCallback callback);
  virtual void GetWriter(
      fuchsia::security::codelabsmartdoor::Token token,
      ::fidl::InterfaceRequest<fuchsia::security::codelabsmartdoor::Writer> request,
      GetWriterCallback callback);
  virtual void Reset(ResetCallback callback);

 private:
  static bool tokenToFilePath(const fuchsia::security::codelabsmartdoor::Token* token,
                              std::string* file_path);
  fidl::BindingSet<fuchsia::security::codelabsmartdoor::Writer,
                   std::unique_ptr<SmartDoorMemoryWriter>>
      writer_bindings_;
  fidl::BindingSet<fuchsia::security::codelabsmartdoor::Reader,
                   std::unique_ptr<SmartDoorMemoryReader>>
      reader_bindings_;
};

class SmartDoorMemoryServerApp {
 public:
  explicit SmartDoorMemoryServerApp();

 protected:
  SmartDoorMemoryServerApp(std::unique_ptr<sys::ComponentContext> context);

 private:
  using Memory = fuchsia::security::codelabsmartdoor::Memory;
  using MemoryReset = fuchsia::security::codelabsmartdoor::MemoryReset;
  SmartDoorMemoryServerApp(const SmartDoorMemoryServerApp&) = delete;
  SmartDoorMemoryServerApp& operator=(const SmartDoorMemoryServerApp&) = delete;

  std::unique_ptr<SmartDoorMemoryServer> service_;
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<Memory> bindings_;
  fidl::BindingSet<MemoryReset> reset_bindings_;
};

}  // namespace smart_door_memory
#endif  // SRC_SECURITY_CODELAB_SMART_DOOR_MEMORY_SRC_SMART_DOOR_MEMORY_SERVER_APP_H_
