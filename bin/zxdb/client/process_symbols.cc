// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/process_symbols.h"

namespace zxdb {

ProcessSymbols::ProcessSymbols(Session* session) : ClientObject(session) {}
ProcessSymbols::~ProcessSymbols() = default;

}  // namespace zxdb
