// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ostream>

#include "garnet/bin/flog_viewer/binding.h"
#include "garnet/bin/flog_viewer/channel.h"
#include "lib/media/fidl/flog/flog.fidl.h"

//
// This file declares a bunch of << operator overloads for formatting stuff.
// Unless you want to add new operators, it's sufficient to know that you can
// just use the operators as expected, except that some of the overloads can
// produce multiple lines. Regardless of this, none of the overloads terminate
// the last line.
//
// Each new line starts with begl in order to apply the appropriate indentation.
// The Indenter class is provided to adjust the identation level. Operators
// that take pointers need to handle nullptr.
//

namespace flog {

int ostream_indent_index();

std::ostream& begl(std::ostream& os);

inline std::ostream& indent(std::ostream& os) {
  ++os.iword(ostream_indent_index());
  return os;
}

inline std::ostream& outdent(std::ostream& os) {
  --os.iword(ostream_indent_index());
  return os;
}

struct EntryHeader {
  EntryHeader(const FlogEntryPtr& entry, uint32_t index)
      : entry_(entry), index_(index) {}
  const FlogEntryPtr& entry_;
  uint32_t index_;
};

std::ostream& operator<<(std::ostream& os, const EntryHeader& value);

struct AsAddress {
  explicit AsAddress(uint64_t address) : address_(address) {}
  uint64_t address_;
};

std::ostream& operator<<(std::ostream& os, AsAddress value);

struct AsKoid {
  explicit AsKoid(uint64_t koid) : koid_(koid) {}
  uint64_t koid_;
};

std::ostream& operator<<(std::ostream& os, AsKoid value);

struct AsNiceDateTime {
  explicit AsNiceDateTime(int64_t time_ns) : time_ns_(time_ns) {}
  int64_t time_ns_;
};

std::ostream& operator<<(std::ostream& os, AsNiceDateTime value);

std::ostream& operator<<(std::ostream& os, const Channel& value);

std::ostream& operator<<(std::ostream& os, const ChildBinding& value);

std::ostream& operator<<(std::ostream& os, const PeerBinding& value);

}  // namespace flog
