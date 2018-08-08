// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/frame.h"

namespace zxdb {

Frame::Frame(Session* session) : ClientObject(session), weak_factory_(this) {}
Frame::~Frame() = default;

fxl::WeakPtr<Frame> Frame::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

}  // namespace zxdb
