// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_STATUS_H_
#define SRC_LIB_FIDL_CODEC_STATUS_H_

#include <string>

#include "zircon/types.h"

namespace fidl_codec {

std::string StatusName(zx_status_t status);

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_STATUS_H_
