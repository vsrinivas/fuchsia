// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/cpp/channel_read.h>
#include <zircon/assert.h>

namespace fdf {

ChannelReadBase::ChannelReadBase(fdf_handle_t channel, uint32_t options,
                                 fdf_channel_read_handler_t* handler)
    : channel_read_{{ASYNC_STATE_INIT}, handler, channel, options} {}

ChannelReadBase::~ChannelReadBase() { ZX_DEBUG_ASSERT(!dispatcher_); }

zx_status_t ChannelReadBase::Begin(fdf_dispatcher_t* dispatcher) {
  {
    std::lock_guard<std::mutex> lock(lock_);

    if (dispatcher_) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    dispatcher_ = dispatcher;
  }

  zx_status_t status = fdf_channel_wait_async(dispatcher, &channel_read_, channel_read_.options);
  if (status != ZX_OK) {
    std::lock_guard<std::mutex> lock(lock_);
    dispatcher_ = nullptr;
  }
  return status;
}

zx_status_t ChannelReadBase::Cancel() {
  std::lock_guard<std::mutex> lock(lock_);

  // Either the user has not called |Begin|, or the callback was dispatched before
  // we could cancel it.
  if (!dispatcher_) {
    return ZX_ERR_NOT_FOUND;
  }
  // It should be safe to hold the lock while we cancel, as we should not reentrantly
  // call into the driver.
  zx_status_t status = fdf_channel_cancel_wait(channel());

  // Check if we are expecting a callback (in the case of unsynchronized dispatchers),
  // in which case we will not clear |dispatcher_| until the callback is dispatched.
  if (!(fdf_dispatcher_get_options(dispatcher_) & FDF_DISPATCHER_OPTION_UNSYNCHRONIZED)) {
    dispatcher_ = nullptr;
  }
  return status;
}

ChannelRead::ChannelRead(fdf_handle_t channel, uint32_t options, Handler handler)
    : ChannelReadBase(channel, options, &ChannelRead::CallHandler), handler_(std::move(handler)) {}

ChannelRead::~ChannelRead() {
  // We do not auto cancel on destruction, as it is possible that on cancellation
  // a callback is still expected, and may be scheduled to occur on a different thread.
  // The user should manually cancel instead and check whether they need to
  // wait for a callback.
  ZX_DEBUG_ASSERT(!is_pending());
}

void ChannelRead::CallHandler(fdf_dispatcher_t* dispatcher, fdf_channel_read_t* read,
                              zx_status_t status) {
  auto self = Dispatch<ChannelRead>(read);
  self->handler_(dispatcher, self, status);
}

}  // namespace fdf
