// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_TOOLS_AUDIO_LISTENER_ESCAPE_DECODER_H_
#define SRC_MEDIA_AUDIO_TOOLS_AUDIO_LISTENER_ESCAPE_DECODER_H_

#include <cstdint>
#include <cstdio>

// This class enables the capturing of four arrow keys. Each is decoded as a three-key sequence
// Esc+[ (Escape OpenBracket) plus 'A' for up, 'B' for down, 'C' for right, 'D' for right.
// No other escape-key sequences are supported.
//
// In state 0, Esc changes state_ to 1 and is otherwise ignored; all other chars are decoded as-is.
// In state 1, [ changes state_ to 2; all others change state_ to 0; all keys are ignored.
// In state 2, A|B|C|D are decoded as arrow keys; all others are ignored; state_ reverts to 0.
class EscapeDecoder {
 public:
  static constexpr int kUpArrow = -10;
  static constexpr int kDownArrow = -11;
  static constexpr int kRightArrow = -12;
  static constexpr int kLeftArrow = -13;

  EscapeDecoder() = default;
  EscapeDecoder(const EscapeDecoder&) = delete;
  EscapeDecoder(EscapeDecoder&&) = delete;
  EscapeDecoder& operator=(const EscapeDecoder&) = delete;
  EscapeDecoder& operator=(EscapeDecoder&&) noexcept;

  int Decode(int c) {
    if (state_ == 2) {
      state_ = 0;
      // clang-format off
      switch (c) {
        case 'A': return kUpArrow;
        case 'B': return kDownArrow;
        case 'C': return kRightArrow;
        case 'D': return kLeftArrow;
        default:  return 0;
      }
      // clang-format on
    }
    if (state_ == 1) {
      state_ = (c == kBracketChar) ? 2 : 0;
      return 0;
    }
    if (c == kEscChar) {
      state_ = 1;
      return 0;
    }
    return c;
  }

 private:
  static constexpr int kEscChar = 0x1b;
  static constexpr int kBracketChar = '[';
  uint32_t state_ = 0;
};

#endif  // SRC_MEDIA_AUDIO_TOOLS_AUDIO_LISTENER_ESCAPE_DECODER_H_
