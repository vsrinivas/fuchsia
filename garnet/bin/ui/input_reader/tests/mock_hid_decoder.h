// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_TESTS_MOCK_HID_DECODER_H_
#define GARNET_BIN_UI_INPUT_READER_TESTS_MOCK_HID_DECODER_H_

#include <lib/fit/function.h>
#include <zx/event.h>

#include "garnet/bin/ui/input_reader/hid_decoder.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace mozart {

// Base class for mock HID decoders, which fails every operation but implements
// |GetEvent| properly.
class MockHidDecoder : public HidDecoder {
 public:
  // Function mocking out |Init()|, which produces the |Protocol| of the device
  // and the bool return value of |Init()| (true on success).
  using InitHandler = fit::function<std::pair<Protocol, bool>()>;

  MockHidDecoder(InitHandler init_handler);
  // Convenience constructor that just sets the protocol on |Init()|.
  MockHidDecoder(Protocol protocol);
  ~MockHidDecoder() override;

  fxl::WeakPtr<MockHidDecoder> GetWeakPtr();

  // |HidDecoder|
  const std::string& name() const override;

  // |HidDecoder|
  Protocol protocol() const override { return protocol_; }

  // |HidDecoder|
  bool Init() override;
  // |HidDecoder|
  zx::event GetEvent() override;

  // |HidDecoder|
  const std::vector<uint8_t>& Read(int* bytes_read) override;
  // |HidDecoder|
  bool Read(HidGamepadSimple* gamepad) override;
  // |HidDecoder|
  bool Read(HidAmbientLightSimple* light) override;
  // |HidDecoder|
  bool Read(HidButtons* data) override;
  // |HidDecoder|
  bool Read(Touchscreen::Report* report) override;
  // |HidDecoder|
  bool Read(Mouse::Report* report) override;

  bool SetDescriptor(Touchscreen::Descriptor* touch_desc) override;

  // Legacy emulation, which will allow |Read(int* bytes_read)| returning
  // |bytes| and setting |bytes_read| to |content_length|. There must not be a
  // pending report that has not been |Read|.
  void Send(std::vector<uint8_t> bytes, int content_length);
  // There must not be a pending report that has not been |Read|.
  void Send(const HidGamepadSimple& gamepad);
  // There must not be a pending report that has not been |Read|.
  void Send(const HidAmbientLightSimple& light);
  // There must not be a pending report that has not been |Read|.
  void Send(const HidButtons& buttons);
  // There must not be a pending report that has not been |Read|.
  void Send(const Touchscreen::Report& touchscreen);
  // There must not be a pending report that has not been |Read|.
  void Send(const Mouse::Report& report);
  // Emulates device removal. There must not be a pending report that has not
  // been |Read|.
  void Close();

 private:
  enum class ReportType {
    kNone = 0,
    kLegacy,
    kGamepad,
    kLight,
    kButtons,
    kTouchscreen,
    kMouse,
  };

  struct Report {
    ReportType type;

    // Keep this outside of the union/don't use std::variant because the legacy
    // |Read| overload needs to return a const ref, so this needs to linger even
    // after the report has been read.
    std::vector<uint8_t> legacy_bytes;
    union {
      // This can be shorter than the length of |legacy_bytes|.
      int legacy_content_length;

      HidGamepadSimple gamepad;
      HidAmbientLightSimple light;
      HidButtons buttons;
      Touchscreen::Report touchscreen;
      Mouse::Report mouse;
    };
  };

  template <class ReportVariant>
  bool MockRead(ReportType expected_type, const ReportVariant& source,
                ReportVariant* dest);

  void Signal();
  void ClearReport();

  InitHandler init_handler_;
  zx::event event_;
  Protocol protocol_;
  Report report_;

  fxl::WeakPtrFactory<MockHidDecoder> weak_ptr_factory_;
};

}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_TESTS_MOCK_HID_DECODER_H_
