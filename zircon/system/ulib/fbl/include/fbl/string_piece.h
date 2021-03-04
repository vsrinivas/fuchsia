// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_STRING_PIECE_H_
#define FBL_STRING_PIECE_H_

#include <string_view>

namespace fbl {

// TODO(70402): Remove outright.
using StringPiece = std::string_view;

}  // namespace fbl

#endif  // FBL_STRING_PIECE_H_
