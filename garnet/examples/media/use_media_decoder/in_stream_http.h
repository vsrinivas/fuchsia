// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "in_stream.h"

#include <fuchsia/net/oldhttp/cpp/fidl.h>

class InStreamHttp : public InStream {
public:
  InStreamHttp(async::Loop* fidl_loop,
               thrd_t fidl_thread,
               component::StartupContext* startup_context,
               std::string url);

  ~InStreamHttp();

private:
  zx_status_t ReadBytesInternal(uint32_t max_bytes_to_read,
                                uint32_t* bytes_read_out,
                                uint8_t* buffer_out,
                                zx::time deadline) override;

  const std::string url_;
  fuchsia::net::oldhttp::URLLoaderPtr url_loader_;
  // The Response.body.stream socket.
  zx::socket socket_;
};
