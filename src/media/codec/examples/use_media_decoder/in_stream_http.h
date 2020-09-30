// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_IN_STREAM_HTTP_H_
#define SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_IN_STREAM_HTTP_H_

#include <fuchsia/net/http/cpp/fidl.h>

#include "in_stream.h"

class InStreamHttp : public InStream {
 public:
  InStreamHttp(async::Loop* fidl_loop, thrd_t fidl_thread, sys::ComponentContext* component_context,
               std::string url);

  ~InStreamHttp() override;

 private:
  zx_status_t ReadBytesInternal(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                                uint8_t* buffer_out, zx::time deadline) override;

  zx_status_t ResetToStartInternal(zx::time just_fail_deadline) override;

  const std::string url_;
  fuchsia::net::http::LoaderPtr http_loader_;
  // The Response.body.stream socket.
  zx::socket socket_;
};

#endif  // SRC_MEDIA_CODEC_EXAMPLES_USE_MEDIA_DECODER_IN_STREAM_HTTP_H_
