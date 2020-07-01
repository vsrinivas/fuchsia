// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/logger.h"

namespace fidl_codec::logger::internal {

thread_local std::ostream* log_stream_tls = nullptr;

}  // namespace fidl_codec::logger::internal
