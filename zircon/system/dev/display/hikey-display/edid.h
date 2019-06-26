// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_HIKEY_DISPLAY_EDID_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_HIKEY_DISPLAY_EDID_H_

#include "hidisplay.h"

namespace hi_display {

class HiEdid {
 public:
  zx_status_t EdidGetNumDtd(const uint8_t* edid_buf_, uint8_t* num_dtd);
  zx_status_t EdidParseDisplayTiming(const uint8_t* edid_buf_, DetailedTiming* raw_dtd,
                                     DisplayTiming* disp_timing, uint8_t num_dtd);
  zx_status_t EdidParseStdDisplayTiming(const uint8_t* edid_buf_, DetailedTiming* raw_dtd,
                                        DisplayTiming* disp_timing);

 private:
  void EdidDumpDispTiming(const DisplayTiming* d);
};

}  // namespace hi_display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_HIKEY_DISPLAY_EDID_H_
