// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "edid.h"

#include <lib/edid/edid.h>
#include <string.h>

namespace hi_display {

zx_status_t HiEdid::EdidGetNumDtd(const uint8_t* edid_buf_, uint8_t* num_dtd) {
  const uint8_t* start_ext;
  const uint8_t* start_dtd;
  *num_dtd = 0;

  edid::BaseEdid* EdidBuf = (edid::BaseEdid*)edid_buf_;

  if (!EdidBuf->num_extensions) {
    return ZX_OK;
  }

  // It has extension. Read from start of DTD until you hit 00 00
  start_ext = &edid_buf_[128];

  if (start_ext[0] != 0x2) {
    zxlogf(ERROR, "%s: Unknown tag! %d\n", __FUNCTION__, start_ext[0]);
    return ZX_ERR_WRONG_TYPE;
  }

  if (start_ext[2] == 0) {
    zxlogf(ERROR, "%s: Invalid DTD pointer! 0x%x\n", __FUNCTION__, start_ext[2]);
    return ZX_ERR_WRONG_TYPE;
  }

  int i = 0;
  start_dtd = &start_ext[0] + start_ext[2];
  while (start_dtd[i] != 0x0 && start_dtd[i + 1] != 0x0) {
    ++(*num_dtd);
    i += 18;
  }

  return ZX_OK;
}

void HiEdid::EdidDumpDispTiming(const DisplayTiming* d) {
  zxlogf(INFO, "%s\n", __FUNCTION__);

  zxlogf(INFO, "pixel_clk = 0x%x\n", d->pixel_clk);
  zxlogf(INFO, "HActive = 0x%x\n", d->HActive);
  zxlogf(INFO, "HBlanking = 0x%x\n", d->HBlanking);
  zxlogf(INFO, "VActive = 0x%x\n", d->VActive);
  zxlogf(INFO, "VBlanking = 0x%x\n", d->VBlanking);
  zxlogf(INFO, "HSyncOffset = 0x%x\n", d->HSyncOffset);
  zxlogf(INFO, "HSyncPulseWidth = 0x%x\n", d->HSyncPulseWidth);
  zxlogf(INFO, "VSyncOffset = 0x%x\n", d->VSyncOffset);
  zxlogf(INFO, "VSyncPulseWidth = 0x%x\n", d->VSyncPulseWidth);
  zxlogf(INFO, "HImageSize = 0x%x\n", d->HImageSize);
  zxlogf(INFO, "VImageSize = 0x%x\n", d->VImageSize);
  zxlogf(INFO, "HBorder = 0x%x\n", d->HBorder);
  zxlogf(INFO, "VBorder = 0x%x\n", d->VBorder);
  zxlogf(INFO, "Flags = 0x%x\n", d->Flags);
}

zx_status_t HiEdid::EdidParseStdDisplayTiming(const uint8_t* edid_buf_, DetailedTiming* raw,
                                              DisplayTiming* d) {
  const uint8_t* start_dtd;
  uint8_t* s_r_dtd = (uint8_t*)raw;

  start_dtd = &edid_buf_[0x36];

  // populate raw structure first
  memcpy(s_r_dtd, start_dtd, 18);

  d->pixel_clk = static_cast<uint16_t>(raw->raw_pixel_clk[1] << 8 | raw->raw_pixel_clk[0]);
  d->HActive = static_cast<uint16_t>((((raw->raw_Hact_HBlank & 0xf0) >> 4) << 8) | raw->raw_Hact);
  d->HBlanking = static_cast<uint16_t>(((raw->raw_Hact_HBlank & 0x0f) << 8) | raw->raw_HBlank);
  d->VActive = static_cast<uint16_t>((((raw->raw_Vact_VBlank & 0xf0) >> 4) << 8) | raw->raw_Vact);
  d->VBlanking = static_cast<uint16_t>(((raw->raw_Vact_VBlank & 0x0f) << 8) | raw->raw_VBlank);
  d->HSyncOffset =
      static_cast<uint16_t>((((raw->raw_HSync_VSync_OFF_PW & 0xc0) >> 6) << 8) | raw->raw_HSyncOff);
  d->HSyncPulseWidth =
      static_cast<uint16_t>((((raw->raw_HSync_VSync_OFF_PW & 0x30) >> 4) << 8) | raw->raw_HSyncPW);
  d->HImageSize =
      static_cast<uint16_t>((((raw->raw_H_V_ImageSize & 0xf0) >> 4) << 8) | raw->raw_HImageSize);
  d->VImageSize =
      static_cast<uint16_t>(((raw->raw_H_V_ImageSize & 0x0f) << 8) | raw->raw_VImageSize);
  d->VSyncOffset = static_cast<uint8_t>((((raw->raw_HSync_VSync_OFF_PW & 0x0c) >> 2) << 4) |
                                        (raw->raw_VSyncOff_VSyncPW & 0xf0) >> 4);
  d->VSyncPulseWidth = static_cast<uint8_t>(((raw->raw_HSync_VSync_OFF_PW & 0x03) << 4) |
                                            (raw->raw_VSyncOff_VSyncPW & 0x0f));
  d->HBorder = raw->raw_HBorder;
  d->VBorder = raw->raw_VBorder;
  d->Flags = raw->raw_Flags;

  return ZX_OK;
}

zx_status_t HiEdid::EdidParseDisplayTiming(const uint8_t* edid_buf_, DetailedTiming* raw,
                                           DisplayTiming* d, uint8_t num_dtd) {
  const uint8_t* start_ext;
  const uint8_t* start_dtd;
  uint8_t* s_r_dtd = (uint8_t*)raw;

  edid::BaseEdid* EdidBuf = (edid::BaseEdid*)edid_buf_;

  if (!EdidBuf->num_extensions) {
    return ZX_ERR_INVALID_ARGS;
  }

  // It has extension. Read from start of DTD until you hit 00 00
  start_ext = &edid_buf_[128];

  if (start_ext[0] != 0x2) {
    zxlogf(ERROR, "%s: Unknown tag! %d\n", __FUNCTION__, start_ext[0]);
    return ZX_ERR_WRONG_TYPE;
  }

  if (start_ext[2] == 0) {
    zxlogf(ERROR, "%s: Invalid DTD pointer! 0x%x\n", __FUNCTION__, start_ext[2]);
    return ZX_ERR_WRONG_TYPE;
  }

  start_dtd = &start_ext[0] + start_ext[2];

  for (int i = 0; i < num_dtd; i++) {
    // populate raw structure first
    memcpy(s_r_dtd, start_dtd, 18);

    d[i].pixel_clk = static_cast<uint16_t>(raw[i].raw_pixel_clk[1] << 8 | raw[i].raw_pixel_clk[0]);
    d[i].HActive =
        static_cast<uint16_t>((((raw[i].raw_Hact_HBlank & 0xf0) >> 4) << 8) | raw[i].raw_Hact);
    d[i].HBlanking =
        static_cast<uint16_t>(((raw[i].raw_Hact_HBlank & 0x0f) << 8) | raw[i].raw_HBlank);
    d[i].VActive =
        static_cast<uint16_t>((((raw[i].raw_Vact_VBlank & 0xf0) >> 4) << 8) | raw[i].raw_Vact);
    d[i].VBlanking =
        static_cast<uint16_t>(((raw[i].raw_Vact_VBlank & 0x0f) << 8) | raw[i].raw_VBlank);
    d[i].HImageSize = static_cast<uint16_t>((((raw[i].raw_H_V_ImageSize & 0xf0) >> 4) << 8) |
                                            raw[i].raw_HImageSize);
    d[i].VImageSize =
        static_cast<uint16_t>(((raw[i].raw_H_V_ImageSize & 0x0f) << 8) | raw[i].raw_VImageSize);
    d[i].HSyncPulseWidth = static_cast<uint16_t>(
        (((raw[i].raw_HSync_VSync_OFF_PW & 0x30) >> 4) << 8) | raw[i].raw_HSyncPW);
    d[i].HSyncOffset = static_cast<uint16_t>((((raw[i].raw_HSync_VSync_OFF_PW & 0xc0) >> 6) << 8) |
                                             raw[i].raw_HSyncOff);
    d[i].VSyncOffset = static_cast<uint8_t>((((raw[i].raw_HSync_VSync_OFF_PW & 0x0c) >> 2) << 4) |
                                            (raw[i].raw_VSyncOff_VSyncPW & 0xf0) >> 4);
    d[i].VSyncPulseWidth = static_cast<uint8_t>(((raw[i].raw_HSync_VSync_OFF_PW & 0x03) << 4) |
                                                (raw[i].raw_VSyncOff_VSyncPW & 0x0f));
    d[i].HBorder = raw[i].raw_HBorder;
    d[i].VBorder = raw[i].raw_VBorder;
    d[i].Flags = raw[i].raw_Flags;
    s_r_dtd += 18;
    start_dtd += 18;
  }
  return ZX_OK;
}

}  // namespace hi_display
