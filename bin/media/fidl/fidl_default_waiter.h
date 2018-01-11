// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIN_MEDIA_FIDL_FIDL_DEFAULT_WAITER_H_
#define BIN_MEDIA_FIDL_FIDL_DEFAULT_WAITER_H_

#include "garnet/bin/media/fidl/fidl_async_waiter.h"

namespace media {

const FidlAsyncWaiter* GetDefaultAsyncWaiter();

}  // namespace media

#endif  // BIN_MEDIA_FIDL_FIDL_DEFAULT_WAITER_H_
