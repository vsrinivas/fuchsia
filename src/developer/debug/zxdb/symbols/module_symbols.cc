// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/module_symbols.h"

namespace zxdb {

ModuleSymbols::ModuleSymbols() : weak_factory_(this) {}

ModuleSymbols::~ModuleSymbols() {
  if (deletion_cb_)
    deletion_cb_(this);
}

fxl::WeakPtr<ModuleSymbols> ModuleSymbols::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

}  // namespace zxdb
