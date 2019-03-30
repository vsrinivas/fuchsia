// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/frame.h"

namespace zxdb {

Frame::Frame(Session* session) : ClientObject(session) {}
Frame::~Frame() = default;

}  // namespace zxdb
