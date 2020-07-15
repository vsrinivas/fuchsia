// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "smart_door_memory_client.h"

#include <lib/syslog/cpp/macros.h>

namespace smart_door {

using fuchsia::security::codelabsmartdoor::Error;
using fuchsia::security::codelabsmartdoor::Memory_GetReader_Result;
using fuchsia::security::codelabsmartdoor::Memory_GetWriter_Result;
using fuchsia::security::codelabsmartdoor::Reader_Read_Result;
using fuchsia::security::codelabsmartdoor::ReaderSyncPtr;
using fuchsia::security::codelabsmartdoor::Token;
using fuchsia::security::codelabsmartdoor::Writer_Write_Result;
using fuchsia::security::codelabsmartdoor::WriterSyncPtr;

bool SmartDoorMemoryClientImpl::write(const std::vector<uint8_t>& buffer) {
  WriterSyncPtr writer;
  Memory_GetWriter_Result get_writer_result;
  // Copy the token.
  Token token;
  token.set_id(token_.id());
  memory_->GetWriter(std::move(token), writer.NewRequest(), &get_writer_result);
  if (get_writer_result.is_err()) {
    return false;
  }

  Writer_Write_Result write_result;
  writer->Write(buffer, &write_result);
  if (write_result.is_err()) {
    return false;
  }
  if (write_result.response().bytes_written != buffer.size()) {
    return false;
  }
  return true;
}

bool SmartDoorMemoryClientImpl::read(std::vector<uint8_t>& buffer) {
  ReaderSyncPtr reader;
  Memory_GetReader_Result get_reader_result;
  // Copy the token.
  Token token;
  token.set_id(token_.id());
  memory_->GetReader(std::move(token), reader.NewRequest(), &get_reader_result);
  if (get_reader_result.is_err()) {
    if (get_reader_result.err() == Error::INVALID_INPUT) {
      // If smart door memory is empty, return true.
      buffer.clear();
      return true;
    } else {
      return false;
    }
  }

  Reader_Read_Result read_result;
  reader->Read(&read_result);
  if (read_result.is_err()) {
    return false;
  }
  buffer = read_result.response().bytes;
  return true;
}

}  // namespace smart_door
