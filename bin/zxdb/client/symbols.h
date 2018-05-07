// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <functional>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class Location;
struct ModuleSymbolRecord;

class Symbols : public ClientObject {
 public:
  explicit Symbols(Session* session);
  ~Symbols() override;

  // Looks up the symbol information for the given address, and asynchronously
  // provides it to the given callback.
  virtual void ResolveAddress(uint64_t address,
                              std::function<void(Location)> callback) = 0;

  // Symbolizes many addresses at once.
  virtual void ResolveAddresses(std::vector<uint64_t> addresses,
      std::function<void(std::vector<Location>)> callback) = 0;

  // Asynchronously looks up the symbol information for the process and issues
  // the callback with the information for the given modules.
  virtual void GetModuleInfo(
      std::function<void(std::vector<ModuleSymbolRecord> records)>
          callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Symbols);
};

}  // namespace zxdb
