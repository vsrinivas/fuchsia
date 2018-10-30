// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_FDIO_HID_DECODER_H_
#define GARNET_BIN_UI_INPUT_READER_FDIO_HID_DECODER_H_

#include <string>
#include <vector>

#include <fbl/unique_fd.h>

#include "garnet/bin/ui/input_reader/hid_decoder.h"

namespace fzl {
class FdioCaller;
}

namespace hid {
struct ReportField;
}

namespace mozart {

// This is the "real" FDIO implementation of |HidDecoder|.
class FdioHidDecoder : public HidDecoder {
 public:
  // The decoder does not take ownership of the |fd|. InputReader takes
  // care of that.
  // TODO(ES-169): How?
  FdioHidDecoder(const std::string& name, int fd);
  ~FdioHidDecoder() override;

  // |HidDecoder|
  const std::string& name() const override { return name_; }

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

 private:
  struct DataLocator {
    uint32_t begin;
    uint32_t count;
    uint32_t match;
  };

  bool ParseProtocol(const fzl::FdioCaller& caller, Protocol* protocol);
  bool ParseGamepadDescriptor(const hid::ReportField* fields, size_t count);
  bool ParseAmbientLightDescriptor(const hid::ReportField* fields,
                                   size_t count);
  bool ParseButtonsDescriptor(const hid::ReportField* fields, size_t count);

  int fd_;
  const std::string name_;
  Protocol protocol_;
  std::vector<uint8_t> report_;
  std::vector<DataLocator> decoder_;
};

}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_FDIO_HID_DECODER_H_
