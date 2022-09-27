// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/examples/tones/midi_keyboard.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/fit/defer.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>

#include "src/media/audio/examples/tones/midi.h"
#include "src/media/audio/examples/tones/tones.h"

namespace examples {

static const char* const kDevMidiPath = "/dev/class/midi";

std::unique_ptr<MidiKeyboard> MidiKeyboard::Create(Tones* owner) {
  struct dirent* de;
  DIR* dir = opendir(kDevMidiPath);
  if (!dir) {
    if (errno != ENOENT) {
      FX_LOGS(WARNING) << "Error attempting to open \"" << kDevMidiPath << "\" (errno " << errno
                       << ")";
    }
    return nullptr;
  }

  auto cleanup = fit::defer([dir]() { closedir(dir); });

  while ((de = readdir(dir)) != nullptr) {
    char devname[128];

    snprintf(devname, sizeof(devname), "%s/%s", kDevMidiPath, de->d_name);
    auto result = component::Connect<fuchsia_hardware_midi::Device>(devname);
    if (!result.is_ok()) {
      continue;
    }
    fidl::ClientEnd<fuchsia_hardware_midi::Device> client(std::move(result.value()));

    auto info_result = fidl::WireCall(client)->GetInfo();
    if (info_result.status() != ZX_OK) {
      FX_LOGS(WARNING) << "fuchsia.hardware.midi.Device/GetInfo failed for \"" << devname << "\"";
      return nullptr;
    }

    if (info_result.value().info.is_source) {
      std::cout << "Creating MIDI source @ \"" << devname << "\"\n";
      std::unique_ptr<MidiKeyboard> ret(new MidiKeyboard(owner, std::move(client)));
      ret->IssueRead();
      return ret;
    }
  }

  return nullptr;
}

void MidiKeyboard::IssueRead() {
  dev_->Read().Then([this](::fidl::WireUnownedResult<fuchsia_hardware_midi::Device::Read>& result) {
    if (!result.ok()) {
      FX_LOGS(WARNING) << "Failed to read from MIDI device: " << result.error();
      return;
    }
    if (result->is_error()) {
      FX_LOGS(WARNING) << "Shutting down MIDI keyboard (status " << result->error_value() << " )";
      return;
    }
    HandleRead(*result->value());
  });
}

void MidiKeyboard::HandleRead(const fuchsia_hardware_midi::wire::DeviceReadResponse& response) {
  if (response.event.count() == 0) {
    IssueRead();
    return;
  }

  if (response.event.count() > 3) {
    FX_LOGS(WARNING) << "Shutting down MIDI keyboard, bad event size (" << response.event.count()
                     << ")";
    return;
  }

  std::array<uint8_t, 3> event;
  memcpy(event.data(), response.event.data(), response.event.count());

  // In theory, USB MIDI event sizes are always supposed to be 4 bytes.  1
  // byte for virtual MIDI cable IDs, and then 3 bytes of the MIDI event
  // padded using 0s to normalize the size.  The Fuchsia USB MIDI driver is
  // currently stripping the first byte and passing all virtual cable events
  // along as the same, but the subsequent bytes may or may not be there.
  //
  // For now, we fill our RX buffers with zero before reading, and attempt
  // to be handle the events in that framework.  Specifically, NOTE_ON events
  // with a 7-bit velocity value of 0 are supposed to be treated as NOTE_OFF
  // values.
  uint8_t cmd = event[0] & MIDI_COMMAND_MASK;
  if ((cmd == MIDI_NOTE_ON) || (cmd == MIDI_NOTE_OFF)) {
    // By default, MIDI event sources map the value 60 to middle C.
    static constexpr int kOffsetMiddleC = 60;
    int note = static_cast<int>(event[1] & MIDI_NOTE_NUMBER_MASK) - kOffsetMiddleC;
    int velocity = static_cast<int>(event[2] & MIDI_NOTE_VELOCITY_MASK);
    bool note_on = (cmd == MIDI_NOTE_ON) && (velocity != 0);

    owner_->HandleMidiNote(note, velocity, note_on);
  }

  IssueRead();
}

}  // namespace examples
