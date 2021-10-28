// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/cpp/channel_read.h>
#include <zircon/assert.h>

namespace fdf {

ChannelReadBase::ChannelReadBase(fdf_handle_t channel, uint32_t options,
                                 fdf_channel_read_handler_t* handler)
    : channel_read_{{ASYNC_STATE_INIT}, handler, channel, options} {}

ChannelReadBase::~ChannelReadBase() {
  // TODO(fxbug.dev/87387) Re-enable this check once we implement cancelling of requests.
  // ZX_DEBUG_ASSERT(!dispatcher_);
}

zx_status_t ChannelReadBase::Begin(fdf_dispatcher_t* dispatcher) {
  if (dispatcher_) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  dispatcher_ = dispatcher;
  fdf_status_t status = fdf_channel_wait_async(dispatcher, &channel_read_, channel_read_.options);
  if (status != ZX_OK) {
    dispatcher_ = nullptr;
  }
  return status;
}

ChannelRead::ChannelRead(fdf_handle_t channel, uint32_t options, Handler handler)
    : ChannelReadBase(channel, options, &ChannelRead::CallHandler), handler_(std::move(handler)) {}

ChannelRead::~ChannelRead() {
  // TODO(fxbug.dev/87387) Re-enable this check once we implement cancelling of requests.
  // ZX_DEBUG_ASSERT(!is_pending());
}

void ChannelRead::CallHandler(fdf_dispatcher_t* dispatcher, fdf_channel_read_t* read,
                              fdf_status_t status) {
  auto self = Dispatch<ChannelRead>(read);
  self->handler_(dispatcher, self, status);
}

}  // namespace fdf
