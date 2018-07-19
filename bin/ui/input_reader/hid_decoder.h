// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <zircon/types.h>

namespace hid {
struct ReportField;
}

namespace mozart {

// This class wraps the file descriptor associated with a HID input
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
    // The ones below are hacks that need to be removed.
    Acer12Touch,
    SamsungTouch,
    ParadiseV1Touch,
    ParadiseV2Touch,
    ParadiseV3Touch,
    EgalaxTouch,
    ParadiseV1TouchPad,
    ParadiseV2TouchPad,
    ParadiseSensor
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

  // The decoder does not take ownership of the |fd|. InputReader takes
  // care of that.
  HidDecoder(const std::string& name, int fd);
  ~HidDecoder() = default;

  const std::string& name() const { return name_; }

  // Inits the internal state. Returns false if any underlying ioctl
  // fails. If so the decoder is not usable. Upon success |out_proto|
  // contains the best guess on the device protocol.
  bool Init();

  // Returns the event that signals when the |fd| is ready to be read.
  bool GetEvent(zx_handle_t* handle);

  // Specifies if clients should use Read(int*) or the new mode which
  // is dependent on the device retuned by protocol().
  bool use_legacy_mode() const;

  // Returns the deviced attached to the |fd|. Only valid after Init()
  Protocol protocol() const { return protocol_; }

  // Reads data from the |fd|. Used when decoding happens on the
  // input interpreter.
  const std::vector<uint8_t>& Read(int* bytes_read);

  // Read data from the |fd| and decodes it into the specified struct.
  // Only valid if Init() out_proto is valid.
  bool Read(HidGamepadSimple* gamepad);
  bool Read(HidAmbientLightSimple* light);

 private:
  struct DataLocator {
    uint32_t begin;
    uint32_t count;
    uint32_t match;
  };

  bool ParseProtocol(Protocol* protocol);
  bool ParseGamepadDescriptor(const hid::ReportField* fields, size_t count);
  bool ParseAmbientLightDescriptor(const hid::ReportField* fields, size_t count);

  const int fd_;
  const std::string name_;
  Protocol protocol_;
  std::vector<uint8_t> report_;
  std::vector<DataLocator> decoder_;
};

}  // namespace mozart
