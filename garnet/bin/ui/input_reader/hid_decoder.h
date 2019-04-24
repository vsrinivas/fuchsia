// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_HID_DECODER_H_
#define GARNET_BIN_UI_INPUT_READER_HID_DECODER_H_

#include <lib/zx/event.h>

#include <string>
#include <vector>

#include "garnet/bin/ui/input_reader/mouse.h"
#include "garnet/bin/ui/input_reader/touch.h"

namespace ui_input {

// This interface wraps the file descriptor associated with a HID input
// device and presents a simpler Read() interface. This is a transitional
// step towards fully wrapping the HID protocol.
class HidDecoder {
 public:
  enum class ReportType { INPUT, OUTPUT, FEATURE };
  // TODO(SCN-867) - The two below enums should be removed when we finally
  // remove all of the Hardcoded devices.
  enum class BootMode {
    NONE,
    MOUSE,
    KEYBOARD,
  };
  enum class Device {
    EYOYO,
    FT3X27,
    SAMSUNG,
  };

  HidDecoder();
  virtual ~HidDecoder();

  virtual const std::string& name() const = 0;

  // Inits the internal state. Returns false if any underlying ioctl
  // fails. If so the decoder is not usable.
  virtual bool Init() = 0;

  // Returns the event that signals when the device is ready to be read.
  virtual zx::event GetEvent() = 0;

  // Checks if the kernel has set a bootmode for the device. If the kernel
  // has, then the hid descriptor and report must follow a specific format.
  // TODO (SCN-1266) - This should be removed when we can just run these
  // through generic HID parsers.
  virtual BootMode ReadBootMode() const = 0;

  // Some devices require that data is sent back to the device. At the moment
  // we don't have a general framework for this so we have hardcoded support
  // for 3 devices. This should be removed when the generic parsers are
  // complete.
  virtual void SetupDevice(Device device) = 0;

  // Reads the Report descriptor from the device.
  virtual const std::vector<uint8_t>& ReadReportDescriptor(int* bytes_read) = 0;

  // Reads a single Report from the device. This will block unless the
  // device has signaled that it is ready to be read.
  virtual const std::vector<uint8_t>& Read(int* bytes_read) = 0;

  // Sends a single Report to the device. |type| must be either
  // OUTPUT or FEATURE.
  virtual zx_status_t Send(ReportType type, uint8_t report_id,
                           const std::vector<uint8_t>& report) = 0;
};

}  // namespace ui_input

#endif  // GARNET_BIN_UI_INPUT_READER_HID_DECODER_H_
