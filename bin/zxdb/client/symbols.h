// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <functional>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

struct ModuleSymbolRecord;
class Symbol;

class Symbols : public ClientObject {
 public:
  Symbols(Session* session);
  ~Symbols() override;

  // Looks up the symbol information for the given address, and asynchronously
  // provides it to the given callback.
  virtual void SymbolAtAddress(uint64_t address,
                               std::function<void(Symbol)> callback) = 0;

  // Asynchronously looks up the symbol information for the process and issues
  // the callback with the information for the given modules.
  virtual void GetModuleInfo(
      std::function<void(std::vector<ModuleSymbolRecord> records)>
          callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Symbols);
};

}  // namespace zxdb
