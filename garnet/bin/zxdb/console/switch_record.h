// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

// Information on a switch provided to a command. This is not for command-line
// switches passed to main().
struct SwitchRecord {
  SwitchRecord() = default;
  SwitchRecord(const SwitchRecord&) = default;
  SwitchRecord(int i, bool has_value, const char* n, char c = 0)
      : id(i), has_value(has_value), name(n), ch(c) {}
  ~SwitchRecord() = default;

  int id = 0;

  // Indicates if this switch has a value. False means it's a bool.
  bool has_value = false;

  // Not including hyphens, e.g. "size" for the switch "--size".
  const char* name = nullptr;

  // 1-character shorthand switch. 0 means no short variant.
  char ch = 0;
};

}  // namespace zxdb
