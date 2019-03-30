// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/client_object.h"

namespace zxdb {

ClientObject::ClientObject(Session* session) : session_(session) {}
ClientObject::~ClientObject() = default;

}  // namespace zxdb
