// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_HID_DECODER_H_
#define GARNET_BIN_UI_INPUT_READER_HID_DECODER_H_

#include <string>
#include <vector>

#include <zx/event.h>

namespace mozart {

// This interface wraps the file descriptor associated with a HID input
// device and presents a simpler Read() interface. This is a transitional
// step towards fully wrapping the HID protocol.
class HidDecoder {
 public:
  enum class Protocol : uint32_t {
    Other,
    Keyboard,
    Mouse,
    Touch,
    Touchpad,
    Gamepad,
    LightSensor,
    Buttons,
    // The ones below are hacks that need to be removed.
    Acer12Touch,
    SamsungTouch,
    ParadiseV1Touch,
    ParadiseV2Touch,
    ParadiseV3Touch,
    EgalaxTouch,
    ParadiseV1TouchPad,
    ParadiseV2TouchPad,
    ParadiseSensor,
    EyoyoTouch,
    Ft3x27Touch,
  };

  struct HidGamepadSimple {
    int32_t left_x;
    int32_t left_y;
    int32_t right_x;
    int32_t right_y;
    uint32_t hat_switch;
  };

  struct HidAmbientLightSimple {
    int16_t illuminance;
  };

  struct HidButtons {
    int8_t volume;
    bool mic_mute;
  };

  HidDecoder();
  virtual ~HidDecoder();

  virtual const std::string& name() const = 0;

  // Inits the internal state. Returns false if any underlying ioctl
  // fails. If so the decoder is not usable. Upon success |protocol()|
  // contains the best guess on the device protocol.
  virtual bool Init() = 0;

  // Returns the event that signals when the device is ready to be read.
  virtual zx::event GetEvent() = 0;

  // Specifies if clients should use Read(int*) or the new mode which
  // is dependent on the device retuned by protocol().
  bool use_legacy_mode() const;

  // Returns a best guess on the device protocol. Only valid after |Init()|.
  virtual Protocol protocol() const = 0;

  // Reads data from the device. Used when decoding happens on the
  // input interpreter (|use_legacy_mode()|).
  virtual const std::vector<uint8_t>& Read(int* bytes_read) = 0;

  // Reads data from the device and decodes it into the specified struct.
  // Only valid if |Init()| is successful.
  virtual bool Read(HidGamepadSimple* gamepad) = 0;
  virtual bool Read(HidAmbientLightSimple* light) = 0;
  virtual bool Read(HidButtons* data) = 0;
};

}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_HID_DECODER_H_
