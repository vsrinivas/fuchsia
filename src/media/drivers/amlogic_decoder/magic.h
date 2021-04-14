// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_MAGIC_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_MAGIC_H_

#include <stdint.h>
#include <zircon/assert.h>

namespace amlogic_decoder {

// Once we're on C++20 we can make MagicNumber a class with a uint64_t in it and avoid the strange
// enum without any declared values.  Also we could keep the file and line around if we wanted
// without breaking ergonomics, if we want.
enum class MagicNumberValue : uint64_t {};

constexpr MagicNumberValue MagicNumber(const char* file, int line) {
  struct impl {
    static constexpr uint32_t FileMagic(const char* file) {
      return file[0] ? FileMagic(file + 1) * 13 + static_cast<uint32_t>(file[0]) : 0;
    }
  };
  return static_cast<MagicNumberValue>(impl::FileMagic(file) + line);
}

template <MagicNumberValue magic_number>
class Magic {
 public:
  Magic() = default;

  Magic(const Magic& to_copy) {
    ZX_ASSERT_MSG(to_copy.magic_ == kMagic,
                  "Magic::Magic(const Magic&) copy source invalid - value: 0x%lx expected 0x%lx",
                  to_copy.magic_, kMagic);
    magic_ = to_copy.magic_;
  }

  Magic& operator=(const Magic& to_copy) {
    ZX_ASSERT_MSG(
        to_copy.magic_ == kMagic,
        "Magic::operator=(const Magic&) copy source invalid - value: 0x%lx expected 0x%lx",
        to_copy.magic_, kMagic);
    magic_ = to_copy.magic_;
    return *this;
  }

  Magic(Magic<magic_number>&& to_move) noexcept {
    ZX_ASSERT_MSG(to_move.magic_ == kMagic,
                  "Magic::Magic(Magic&&) move source invalid - value: 0x%lx expected: 0x%lx",
                  to_move.magic_, kMagic);
    magic_ = to_move.magic_;
    to_move.magic_ = kMovedOut;
  }

  ~Magic() {
    ZX_ASSERT_MSG(magic_ == kMagic || magic_ == kMovedOut,
                  "Magic::~Magic() found corrupted magic - value: 0x%lx expected 0x%lx or 0x%lx",
                  magic_, kMagic, kMovedOut);
    magic_ = kGone;
  }

  void AssertOk() {
    ZX_ASSERT_MSG(magic_ == kMagic, "Magic::AssertOk() failing - value: 0x%lx expected: 0x%lx",
                  magic_, kMagic);
  }

 private:
  using MagicType = uint64_t;
  static constexpr MagicType kMagic = static_cast<MagicType>(magic_number);
  static constexpr MagicType kGone = 0xBADC0DEBADC0DE;
  static constexpr MagicType kMovedOut = 0xDEAD1DEAD1DEAD1;
  MagicType magic_ = kMagic;
};

}  // namespace amlogic_decoder

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_MAGIC_H_
