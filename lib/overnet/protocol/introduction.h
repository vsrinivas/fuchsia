// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <sstream>
#include "garnet/lib/overnet/protocol/varint.h"
#include "garnet/lib/overnet/vocabulary/optional.h"
#include "garnet/lib/overnet/vocabulary/slice.h"
#include "garnet/lib/overnet/vocabulary/status.h"

namespace overnet {

// An Introduction records a request to create a new stream in Overnet.
// It's a key-value store, with a small fixed set of keys (up to 256 are
// allowed).
class Introduction {
 public:
  // Assigned key names
  enum class Key : uint8_t {
    ServiceName = 1,
  };

  Introduction() = default;
  Introduction(std::initializer_list<std::pair<Key, Slice>> aggregate) {
    for (auto &p : aggregate) {
      (*this)[p.first] = p.second;
    }
  }

  Optional<Slice> &operator[](Key key) {
    return values_[static_cast<int>(key)];
  }

  const Optional<Slice> &operator[](Key key) const {
    return values_[static_cast<int>(key)];
  }

  Slice Write(Border desired_border) const {
    uint8_t key_len[256];
    size_t value_len[256];
    size_t frame_len = 0;
    for (int i = 0; i < 256; i++) {
      if (values_[i].has_value()) {
        value_len[i] = values_[i]->length();
        key_len[i] = varint::WireSizeFor(value_len[i]);
        frame_len += 1;
        frame_len += key_len[i];
        frame_len += value_len[i];
      } else {
        key_len[i] = 0;
      }
    }
    return Slice::WithInitializerAndBorders(
        frame_len, desired_border, [&](uint8_t *p) {
          for (int i = 0; i < 256; i++) {
            if (key_len[i] != 0) {
              *p++ = static_cast<uint8_t>(i);
              p = varint::Write(value_len[i], key_len[i], p);
              memcpy(p, values_[i]->begin(), value_len[i]);
              p += value_len[i];
            }
          }
        });
  }

  static StatusOr<Introduction> Parse(Slice slice) {
    int max_seen_id = -1;
    Introduction out;
    const uint8_t *p = slice.begin();
    const uint8_t *end = slice.end();
    while (p != end) {
      int id = *p++;
      if (id <= max_seen_id) {
        return StatusOr<Introduction>(
            StatusCode::FAILED_PRECONDITION,
            "Introduction chunks must be sent in ascending order");
      }
      uint64_t len;
      if (!varint::Read(&p, end, &len)) {
        return StatusOr<Introduction>(
            StatusCode::FAILED_PRECONDITION,
            "Failed to read value length from introduction slice");
      }
      const uint64_t remaining_bytes = uint64_t(end - p);
      if (remaining_bytes < len) {
        std::ostringstream out;
        out << "Introduction value length runs past end of introduction slice: "
               "had "
            << remaining_bytes
            << " left in stream, but encoded stream requested " << len
            << " bytes: " << slice;
        return StatusOr<Introduction>(StatusCode::FAILED_PRECONDITION,
                                      out.str());
      }
      max_seen_id = id;
      out.values_[id] = slice.FromPointer(p).ToOffset(len);
      p += len;
    }
    return out;
  }

  bool operator==(const Introduction &other) const {
    for (int i = 0; i < 256; i++) {
      if (values_[i] != other.values_[i]) {
        return false;
      }
    }
    return true;
  }

 private:
  Optional<Slice> values_[256];
};

}  // namespace overnet