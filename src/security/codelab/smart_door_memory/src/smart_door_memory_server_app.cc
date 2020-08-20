// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This is a fake 'smart door memory' component for security codelab.
// It CONTAINS vulnerability intentionally.
// DO NOT COPY ANY OF THE CODE IN THIS FILE!
#include "smart_door_memory_server_app.h"

#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/time.h>

#include <ctime>

#include "dirent.h"
#include "fcntl.h"
#include "sys/stat.h"

namespace smart_door_memory {

using fuchsia::security::codelabsmartdoor::Error;
using fuchsia::security::codelabsmartdoor::Memory_GenerateToken_Response;
using fuchsia::security::codelabsmartdoor::Memory_GenerateToken_Result;
using fuchsia::security::codelabsmartdoor::Memory_GetReader_Response;
using fuchsia::security::codelabsmartdoor::Memory_GetReader_Result;
using fuchsia::security::codelabsmartdoor::Memory_GetWriter_Response;
using fuchsia::security::codelabsmartdoor::Memory_GetWriter_Result;
using fuchsia::security::codelabsmartdoor::Reader;
using fuchsia::security::codelabsmartdoor::Reader_Read_Response;
using fuchsia::security::codelabsmartdoor::Reader_Read_Result;
using fuchsia::security::codelabsmartdoor::Token;
using fuchsia::security::codelabsmartdoor::TOKEN_ID_SIZE;
using fuchsia::security::codelabsmartdoor::Writer;
using fuchsia::security::codelabsmartdoor::Writer_Write_Response;
using fuchsia::security::codelabsmartdoor::Writer_Write_Result;

const char* STORAGE_FOLDER = "/data/storage/";
const char* LOG_FILE = "/data/log";

SmartDoorMemoryServer::SmartDoorMemoryServer() {
  // Create the storage folder.
  mkdir(STORAGE_FOLDER, 0700);
}

void SmartDoorMemoryServer::GenerateToken(GenerateTokenCallback callback) {
  FX_LOGS(INFO) << "Generating random token" << std::endl;
  Token token;
  uint8_t id[TOKEN_ID_SIZE / 2];
  zx_cprng_draw(id, TOKEN_ID_SIZE / 2);
  char id_hex[TOKEN_ID_SIZE + 1] = {};
  for (size_t i = 0; i < TOKEN_ID_SIZE / 2; i++) {
    snprintf(&id_hex[i * 2], sizeof(id_hex) - (i * 2), "%02X", id[i]);
  }
  token.set_id(std::string(id_hex));
  callback(
      Memory_GenerateToken_Result::WithResponse(Memory_GenerateToken_Response(std::move(token))));
}

bool SmartDoorMemoryServer::Log(std::string file_path, bool read_or_write) {
  std::timespec ts;
  if (!std::timespec_get(&ts, TIME_UTC)) {
    return false;
  }
  zx_time_t now = zx_time_from_timespec(ts);
  char time[256] = {};
  int written = snprintf(time, sizeof(time), "%" PRIi64, now);
  if (written < 0 || written > (int)sizeof(time)) {
    return false;
  }
  std::string log = std::string(time) + " " + file_path;
  if (read_or_write) {
    log += " read";
  } else {
    log += " write";
  }
  log += "\n";
  int fd = 0;
  FX_LOGS(INFO) << "logging to file " << LOG_FILE << std::endl;
  if (fdio_open_fd(LOG_FILE,
                   fuchsia::io::OPEN_RIGHT_WRITABLE | fuchsia::io::OPEN_FLAG_CREATE |
                       fuchsia::io::OPEN_FLAG_APPEND,
                   &fd) != ZX_OK) {
    return false;
  }
  const char* log_c = log.c_str();
  if (write(fd, log_c, strlen(log_c) + 1) != static_cast<ssize_t>(strlen(log_c) + 1)) {
    close(fd);
    return false;
  }
  close(fd);
  return true;
}

bool SmartDoorMemoryServer::tokenToFilePath(const Token* token, std::string* file_path) {
  if (!token->has_id() || token->id().size() != TOKEN_ID_SIZE) {
    return false;
  }
  *file_path = STORAGE_FOLDER + token->id();
  return true;
}

void SmartDoorMemoryServer::GetReader(Token token, ::fidl::InterfaceRequest<Reader> request,
                                      GetReaderCallback callback) {
  FX_LOGS(INFO) << "Getting reader" << std::endl;
  std::string file_path;
  if (!tokenToFilePath(&token, &file_path)) {
    callback(Memory_GetReader_Result::WithErr(Error::INVALID_INPUT));
    return;
  }
  int fd = open(file_path.c_str(), O_RDONLY);
  if (fd < 0) {
    callback(Memory_GetReader_Result::WithErr(Error::INVALID_INPUT));
    return;
  }
  reader_bindings_.AddBinding(std::make_unique<SmartDoorMemoryReader>(fd, std::move(file_path)),
                              std::move(request));
  callback(Memory_GetReader_Result::WithResponse(Memory_GetReader_Response()));
}

void SmartDoorMemoryServer::GetWriter(Token token, ::fidl::InterfaceRequest<Writer> request,
                                      GetWriterCallback callback) {
  FX_LOGS(INFO) << "Getting writer" << std::endl;
  std::string file_path;
  if (!tokenToFilePath(&token, &file_path)) {
    callback(Memory_GetWriter_Result::WithErr(Error::INVALID_INPUT));
    return;
  }
  int fd = open(file_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0) {
    callback(Memory_GetWriter_Result::WithErr(Error::INVALID_INPUT));
    return;
  }
  writer_bindings_.AddBinding(std::make_unique<SmartDoorMemoryWriter>(fd, std::move(file_path)),
                              std::move(request));
  callback(Memory_GetWriter_Result::WithResponse(Memory_GetWriter_Response()));
}

void SmartDoorMemoryServer::Reset(ResetCallback callback) {
  // Erase every files under the storage folder.
  DIR* dir = opendir(STORAGE_FOLDER);
  ZX_ASSERT(dir);
  struct dirent* de;
  while ((de = readdir(dir)) != NULL) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
      continue;

    char tmp[PATH_MAX];
    tmp[0] = 0;
    size_t bytes_left = PATH_MAX - 1;
    strncat(tmp, STORAGE_FOLDER, bytes_left);
    bytes_left -= strlen(STORAGE_FOLDER);
    strncat(tmp, "/", bytes_left);
    bytes_left--;
    strncat(tmp, de->d_name, bytes_left);
    unlink(tmp);
  }
  closedir(dir);

  // Erase log file.
  unlink(LOG_FILE);

  callback();
  return;
}

void SmartDoorMemoryWriter::Write(std::vector<uint8_t> data, WriteCallback callback) {
  if (lseek(fd_, 0, SEEK_SET) == -1) {
    callback(Writer_Write_Result::WithErr(Error::INVALID_INPUT));
    return;
  }
  if (ftruncate(fd_, 0) == -1) {
    callback(Writer_Write_Result::WithErr(Error::INVALID_INPUT));
    return;
  }
  ssize_t written = write(fd_, data.data(), data.size());
  if (written != static_cast<ssize_t>(data.size())) {
    callback(Writer_Write_Result::WithErr(Error::INVALID_INPUT));
    return;
  }
  sync();

  if (!SmartDoorMemoryServer::Log(file_path_, false)) {
    callback(Writer_Write_Result::WithErr(Error::INTERNAL));
    return;
  }
  callback(Writer_Write_Result::WithResponse(Writer_Write_Response(written)));
  return;
}

void SmartDoorMemoryReader::Read(ReadCallback callback) {
  // Get file size.
  ssize_t size = lseek(fd_, 0, SEEK_END);
  std::vector<uint8_t> data;
  data.resize(size);
  if (lseek(fd_, 0, SEEK_SET) == -1) {
    callback(Reader_Read_Result::WithErr(Error::INVALID_INPUT));
    return;
  }
  ssize_t read_bytes = read(fd_, data.data(), data.size());
  if (read_bytes != static_cast<ssize_t>(data.size())) {
    callback(Reader_Read_Result::WithErr(Error::INVALID_INPUT));
    return;
  }
  if (!SmartDoorMemoryServer::Log(file_path_, true)) {
    callback(Reader_Read_Result::WithErr(Error::INTERNAL));
    return;
  }
  callback(Reader_Read_Result::WithResponse(Reader_Read_Response(std::move(data))));
  return;
}

SmartDoorMemoryServerApp::SmartDoorMemoryServerApp()
    : SmartDoorMemoryServerApp(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

SmartDoorMemoryServerApp::SmartDoorMemoryServerApp(std::unique_ptr<sys::ComponentContext> context)
    : service_(new SmartDoorMemoryServer()), context_(std::move(context)) {
  context_->outgoing()->AddPublicService(bindings_.GetHandler(service_.get()));
  context_->outgoing()->AddPublicService(reset_bindings_.GetHandler(service_.get()));
}

}  // namespace smart_door_memory
