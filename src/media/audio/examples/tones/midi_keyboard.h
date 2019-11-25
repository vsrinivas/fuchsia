// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_EXAMPLES_TONES_MIDI_KEYBOARD_H_
#define SRC_MEDIA_AUDIO_EXAMPLES_TONES_MIDI_KEYBOARD_H_

#include <memory>

#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/tasks/fd_waiter.h"

namespace examples {

class Tones;

class MidiKeyboard {
 public:
  // Attempt open and use the first MIDI event source we encounter.
  static std::unique_ptr<MidiKeyboard> Create(Tones* owner);

 private:
  friend std::unique_ptr<MidiKeyboard>::deleter_type;

  MidiKeyboard(Tones* owner, fbl::unique_fd dev) : owner_(owner), dev_(std::move(dev)) {}
  ~MidiKeyboard();

  void Wait();
  void HandleEvent();

  Tones* const owner_;
  const fbl::unique_fd dev_;
  fsl::FDWaiter fd_waiter_;
  bool waiting_ = false;
};

}  // namespace examples

#endif  // SRC_MEDIA_AUDIO_EXAMPLES_TONES_MIDI_KEYBOARD_H_
