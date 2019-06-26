// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/vocabulary/slice.h"

#include <iomanip>
#include <sstream>

namespace overnet {

namespace {
std::vector<std::pair<uint8_t, size_t>> RLE(const uint8_t* begin,
                                            const uint8_t* end) {
  uint16_t cur = 256;
  size_t count = 0;
  std::vector<std::pair<uint8_t, size_t>> out;
  auto flush = [&](uint16_t new_cur) {
    if (cur >= 256) {
      assert(count == 0);
    } else {
      if (count < 8) {
        for (size_t i = 0; i < count; i++) {
          out.push_back(std::make_pair(cur, 1));
        }
      } else {
        out.push_back(std::make_pair(cur, count));
      }
    }
    cur = new_cur;
    count = 0;
  };
  for (const auto* p = begin; p != end; ++p) {
    if (*p != cur) {
      flush(*p);
    }
    count++;
  }
  flush(256);
  return out;
}
}  // namespace

std::ostream& operator<<(std::ostream& out, const Border& border) {
  return out << "<" << border.prefix << "+++" << border.suffix << ">";
}

std::ostream& operator<<(std::ostream& out, const Slice& slice) {
  bool first = true;
  std::ostringstream temp;
  auto rle = RLE(slice.begin(), slice.end());
  for (auto b : rle) {
    if (!first)
      temp << ' ';
    temp << std::hex << std::setfill('0') << std::setw(2)
         << static_cast<unsigned>(b.first);
    if (b.second != 1) {
      assert(b.second != 0);
      temp << '*' << std::dec << b.second;
    }
    first = false;
  }
  if (!first)
    temp << ' ';
  temp << '"';
  for (auto b : rle) {
    const auto c = static_cast<char>(b.first);
    if (isprint(c)) {
      temp << c;
    } else {
      temp << '.';
    }
    if (b.second != 1) {
      assert(b.second != 0);
      temp << '*' << std::dec << b.second;
    }
  }
  temp << '"';
  return out << '[' << temp.str() << ']';
}

std::ostream& operator<<(std::ostream& out, const std::vector<Slice>& slices) {
  out << "[";
  bool first = true;
  for (const auto& s : slices) {
    if (!first)
      out << ", ";
    first = false;
    out << s;
  }
  return out << "]";
}

std::ostream& operator<<(std::ostream& out, const Chunk& chunk) {
  return out << "@" << chunk.offset << chunk.slice;
}

}  // namespace overnet
