// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <zircon/types.h>

namespace mozart {

// This class wraps the file descriptor associated with a HID input
// device and presents a simpler Read() interface. This is a transitional
// step towards fully wrapping the HID protocol.
class HidDecoder {
public:
  // The decoder does not take ownership of the |fd|. InputReader takes
  // care of that.
  HidDecoder(const std::string& name, int fd);
  ~HidDecoder() = default;

  const std::string& name() const { return name_; }

  // Inits the internal buffers. Returns false if any underlying ioctl
  // fails. If so the decoder is not usable.
  bool Init(int* out_proto);

  // Returns the event that signals when the |fd| is ready to be read.
  bool GetEvent(zx_handle_t* handle);
  // Reads datat from the |fd|
  const std::vector<uint8_t>& Read(int* bytes_read);

  // TODO(cpu): Make these 3 methods private and then remove them
  // once real HID parsing is integrated.
  bool GetReportDescriptionLength(size_t* out_report_desc_len);
  bool GetReportDescription(uint8_t* out_buf, size_t out_report_desc_len);
  void apply_samsung_touch_hack();

private:
  const int fd_;
  const std::string name_;
  std::vector<uint8_t> report_;
};

}  // namespace mozart