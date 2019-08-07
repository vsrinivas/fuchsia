// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "in_stream.h"

#include "util.h"

#include <fstream>

class InStreamFile : public InStream {
public:
  InStreamFile(async::Loop* fidl_loop,
               thrd_t fidl_thread,
               component::StartupContext* startup_context,
               std::string input_file_name);
  ~InStreamFile();

private:
  zx_status_t ReadBytesInternal(uint32_t max_bytes_to_read,
                                        uint32_t* bytes_read_out,
                                        uint8_t* buffer_out,
                                        zx::time deadline =
                                            zx::time::infinite()) override;

  std::string input_file_name_;
  std::ifstream file_;
};
