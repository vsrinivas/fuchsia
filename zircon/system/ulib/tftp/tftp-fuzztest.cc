// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <tftp/tftp.h>

#include "internal.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
  uint8_t sess_buf[sizeof(tftp_session)];
  tftp_session* session = nullptr;
  if (tftp_init(&session, sess_buf, sizeof(tftp_session)) != TFTP_NO_ERROR) {
    printf("tftp_init failed\n");
    return 0;
  }

  auto open_read_fn = [](const char* filename, void* cookie) -> ssize_t { return 0; };
  auto open_write_fn = [](const char* filename, size_t size, void* cookie) -> tftp_status {
    return TFTP_NO_ERROR;
  };
  auto read_fn = [](void* data, size_t* len, off_t offset, void* cookie) -> tftp_status {
    return TFTP_NO_ERROR;
  };
  auto write_fn = [](const void* data, size_t* len, off_t offset, void* cookie) -> tftp_status {
    return TFTP_NO_ERROR;
  };
  auto close_fn = [](void* cookie) { return; };
  tftp_file_interface ifc = {open_read_fn, open_write_fn, read_fn, write_fn, close_fn};
  if (tftp_session_set_file_interface(session, &ifc) != TFTP_NO_ERROR) {
    return 0;
  }

  uint16_t* block_size_ptr = nullptr;
  uint16_t block_size;
  if (Size >= 2) {
    memcpy(&block_size, Data, 2);
    block_size_ptr = &block_size;
    Size -= 2;
    Data += 2;
  }
  uint8_t* timeout_ptr = nullptr;
  uint8_t timeout;
  if (Size >= 1) {
    memcpy(&timeout, Data, 1);
    timeout_ptr = &timeout;
    Size -= 1;
    Data += 1;
  }
  uint16_t* window_size_ptr = nullptr;
  uint16_t window_size;
  if (Size >= 2) {
    memcpy(&window_size, Data, 2);
    window_size_ptr = &window_size;
    Size -= 2;
    Data += 2;
  }
  if (tftp_set_options(session, block_size_ptr, timeout_ptr, window_size_ptr) != TFTP_NO_ERROR) {
    printf("tftp_set_options failed\n");
    return 0;
  }

  size_t scratch_size = 2048;
  uint8_t scratch[2048];
  uint32_t timeout_ms = 0;
  tftp_process_msg(session, reinterpret_cast<void*>(const_cast<uint8_t*>(Data)), Size, scratch,
                   &scratch_size, &timeout_ms, nullptr);
  return 0;
}
