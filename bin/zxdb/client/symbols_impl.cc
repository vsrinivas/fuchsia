// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols_impl.h"

#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

SymbolsImpl::SymbolsImpl(Session* session)
    : Symbols(session) {}

SymbolsImpl::~SymbolsImpl() {
}

void SymbolsImpl::AddModule(const debug_ipc::Module& module,
                            std::function<void(const std::string&)> callback) {
}

void SymbolsImpl::SetModules(const std::vector<debug_ipc::Module>& modules) {
}

}  // namespace zxdb
