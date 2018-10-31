// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/tests/mock_hid_decoder.h"

#include "lib/fxl/logging.h"

namespace mozart {

namespace {

const std::string kDeviceName = "MockHidDecoder";

}

MockHidDecoder::MockHidDecoder(InitHandler init_handler)
    : init_handler_(std::move(init_handler)), weak_ptr_factory_(this) {}
MockHidDecoder::MockHidDecoder(Protocol protocol)
    : MockHidDecoder(
          [=] { return std::pair<Protocol, bool>(protocol, true); }) {}
MockHidDecoder::~MockHidDecoder() = default;

fxl::WeakPtr<MockHidDecoder> MockHidDecoder::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

const std::string& MockHidDecoder::name() const { return kDeviceName; }

bool MockHidDecoder::Init() {
  std::pair<Protocol, bool> result = init_handler_();
  protocol_ = result.first;
  return result.second;
}

zx::event MockHidDecoder::GetEvent() {
  zx::event dup;
  zx::event::create(0, &event_);
  event_.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ, &dup);
  // If any errors occur, returning ZX_HANDLE_INVALID is fine.
  return dup;
}

const std::vector<uint8_t>& MockHidDecoder::Read(int* bytes_read) {
  FXL_CHECK(report_.type == ReportType::kLegacy ||
            report_.type == ReportType::kNone);

  if (report_.type == ReportType::kLegacy) {
    *bytes_read = report_.legacy_content_length;
    ClearReport();
  } else {
    *bytes_read = -1;
  }

  return report_.legacy_bytes;
}

bool MockHidDecoder::Read(HidGamepadSimple* gamepad) {
  return MockRead(ReportType::kGamepad, report_.gamepad, gamepad);
}

bool MockHidDecoder::Read(HidAmbientLightSimple* light) {
  return MockRead(ReportType::kLight, report_.light, light);
}

bool MockHidDecoder::Read(HidButtons* data) {
  return MockRead(ReportType::kButtons, report_.buttons, data);
}

void MockHidDecoder::Send(std::vector<uint8_t> bytes, int content_length) {
  FXL_CHECK(report_.type == ReportType::kNone);
  report_.type = ReportType::kLegacy;
  report_.legacy_bytes = std::move(bytes);
  report_.legacy_content_length = content_length;
  Signal();
}

void MockHidDecoder::Send(const HidGamepadSimple& gamepad) {
  FXL_CHECK(report_.type == ReportType::kNone);
  report_.type = ReportType::kGamepad;
  report_.gamepad = gamepad;
  Signal();
}

void MockHidDecoder::Send(const HidAmbientLightSimple& light) {
  FXL_CHECK(report_.type == ReportType::kNone);
  report_.type = ReportType::kLight;
  report_.light = light;
  Signal();
}

void MockHidDecoder::Send(const HidButtons& buttons) {
  FXL_CHECK(report_.type == ReportType::kNone);
  report_.type = ReportType::kButtons;
  report_.buttons = buttons;
  Signal();
}

void MockHidDecoder::Close() {
  // Signalling while the device is not readable indicates that it should be
  // removed.
  FXL_CHECK(report_.type == ReportType::kNone);
  Signal();
}

template <class ReportVariant>
bool MockHidDecoder::MockRead(ReportType expected_type,
                              const ReportVariant& source,
                              ReportVariant* dest) {
  FXL_CHECK(report_.type == expected_type || report_.type == ReportType::kNone);

  if (report_.type == expected_type) {
    *dest = source;
    ClearReport();
    return true;
  } else {
    return false;
  }
}

void MockHidDecoder::Signal() {
  FXL_CHECK(event_.signal(0, ZX_USER_SIGNAL_0) == ZX_OK);
}

void MockHidDecoder::ClearReport() {
  report_.type = ReportType::kNone;
  FXL_CHECK(event_.signal(ZX_USER_SIGNAL_0, 0) == ZX_OK);
}

}  // namespace mozart
