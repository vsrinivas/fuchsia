// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/process_symbols.h"

namespace zxdb {

// Provides a ProcessSymbols implementation that just returns empty values for
// everything. Tests can override this to implement the subset of
// functionality they need.
class MockProcessSymbols : public ProcessSymbols {
 public:
  MockProcessSymbols();
  ~MockProcessSymbols() override;

  // ProcessSymbols implementation.
  TargetSymbols* GetTargetSymbols() override;
  std::vector<ModuleStatus> GetStatus() const override;
  Location LocationForAddress(uint64_t address) const override;
  LineDetails LineDetailsForAddress(uint64_t address) const override;
  std::vector<uint64_t> AddressesForFunction(
      const std::string& name) const override;
  std::vector<uint64_t> AddressesForLine(const FileLine& line) const override;
};

}  // namespace
