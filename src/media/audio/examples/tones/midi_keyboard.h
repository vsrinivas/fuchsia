// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_EXAMPLES_TONES_MIDI_KEYBOARD_H_
#define SRC_MEDIA_AUDIO_EXAMPLES_TONES_MIDI_KEYBOARD_H_

#include <fidl/fuchsia.hardware.midi/cpp/wire.h>
#include <lib/async/default.h>

#include <memory>

namespace examples {

class Tones;

class MidiKeyboard {
 public:
  // Attempt open and use the first MIDI event source we encounter.
  static std::unique_ptr<MidiKeyboard> Create(Tones* owner);

 private:
  friend std::unique_ptr<MidiKeyboard>::deleter_type;

  MidiKeyboard(Tones* owner, fidl::ClientEnd<fuchsia_hardware_midi::Device> dev)
      : owner_(owner), dev_(std::move(dev), async_get_default_dispatcher()) {}

  void IssueRead();
  void HandleRead(const fuchsia_hardware_midi::wire::DeviceReadResponse& response);

  Tones* const owner_;
  fidl::WireClient<fuchsia_hardware_midi::Device> dev_;
};

}  // namespace examples

#endif  // SRC_MEDIA_AUDIO_EXAMPLES_TONES_MIDI_KEYBOARD_H_
