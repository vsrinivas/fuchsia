// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZXIO_REMOTE_V2_REMOTE_V2_H_
#define ZIRCON_SYSTEM_ULIB_ZXIO_REMOTE_V2_REMOTE_V2_H_

#include <lib/zx/channel.h>
#include <lib/zx/stream.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/ops.h>

// C++ wrapper around zxio_remote_v2_t.
class RemoteV2 {
 public:
  explicit RemoteV2(zxio_t* io) : rio_(reinterpret_cast<zxio_remote_v2_t*>(io)) {}

  [[nodiscard]] zx::unowned_channel control() const { return zx::unowned_channel(rio_->control); }

  [[nodiscard]] zx::unowned_handle observer() const { return zx::unowned_handle(rio_->observer); }

  [[nodiscard]] zx::unowned_stream stream() const { return zx::unowned_stream(rio_->stream); }

  void Close();

  zx::channel Release();

 private:
  zxio_remote_v2_t* rio_;
};

#endif  // ZIRCON_SYSTEM_ULIB_ZXIO_REMOTE_V2_REMOTE_V2_H_
