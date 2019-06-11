// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_MOCK_FORMAT_VALUE_PROCESS_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_MOCK_FORMAT_VALUE_PROCESS_CONTEXT_H_

#include <map>

#include "src/developer/debug/zxdb/console/format_value.h"
#include "src/developer/debug/zxdb/symbols/location.h"

namespace zxdb {

class Process;

class MockFormatValueProcessContext : public FormatValue::ProcessContext {
 public:
  MockFormatValueProcessContext();
  ~MockFormatValueProcessContext() override;

  // Adds a mock result for a given address query. Must be an exact match.
  void AddResult(uint64_t address, Location location);

  // FormatValue::ProcessContext implementation.
  Location GetLocationForAddress(uint64_t address) const override;

 private:
  std::map<uint64_t, Location> locations_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_MOCK_FORMAT_VALUE_PROCESS_CONTEXT_H_
