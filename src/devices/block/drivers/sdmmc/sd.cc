// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/sdmmc/c/banjo.h>
#include <inttypes.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/debug.h>
#include <ddk/device.h>

#include "sdmmc-block-device.h"

namespace {

// If this bit is set in the Operating Conditions Register, then we know that
// the card is a SDHC (high capacity) card.
constexpr uint32_t kOcrSdhc = 0xc0000000;

constexpr uint32_t kAcmd41FlagSdhcSdxcSupport = 0x40000000;
constexpr uint32_t kAcmd41FlagVoltageWindowAll = 0x00ff8000;

// The "STRUCTURE" field of the "Card Specific Data" register defines the
// version of the structure and how to interpret the rest of the bits.
constexpr uint8_t kCsdStructV2 = 0x1;

}  // namespace

namespace sdmmc {

zx_status_t SdmmcBlockDevice::ProbeSd() {
  // Issue the SEND_IF_COND command, this will tell us that we can talk to
  // the card correctly and it will also tell us if the voltage range that we
  // have supplied has been accepted.
  zx_status_t st = sdmmc_.SdSendIfCond();
  if (st != ZX_OK) {
    return st;
  }

  // Get the operating conditions from the card.
  uint32_t ocr;
  if ((st = sdmmc_.SdSendOpCond(0, &ocr)) != ZX_OK) {
    zxlogf(ERROR, "sd: SDMMC_SD_SEND_OP_COND failed, retcode = %d", st);
    return st;
  }

  int attempt = 0;
  const int max_attempts = 200;
  bool card_supports_18v_signalling = false;
  while (true) {
    const uint32_t flags = kAcmd41FlagSdhcSdxcSupport | kAcmd41FlagVoltageWindowAll;
    uint32_t ocr;
    if ((st = sdmmc_.SdSendOpCond(flags, &ocr)) != ZX_OK) {
      zxlogf(ERROR, "sd: SD_SEND_OP_COND failed with retcode = %d", st);
      return st;
    }

    if (ocr & (1 << 31)) {
      if (!(ocr & kOcrSdhc)) {
        // Card is not an SDHC card. We currently don't support this.
        zxlogf(ERROR, "sd: unsupported card type, must use sdhc card");
        return ZX_ERR_NOT_SUPPORTED;
      }
      card_supports_18v_signalling = !!((ocr >> 24) & 0x1);
      break;
    }

    if (++attempt == max_attempts) {
      zxlogf(ERROR, "sd: too many attempt trying to negotiate card OCR");
      return ZX_ERR_TIMED_OUT;
    }

    zx::nanosleep(zx::deadline_after(zx::msec(5)));
  }

  st = sdmmc_.host().SetBusFreq(25000000);
  if (st != ZX_OK) {
    // This is non-fatal but the card will run slowly.
    zxlogf(ERROR, "sd: failed to increase bus frequency.");
  }

  // TODO(bradenkell): Re-enable support for UHS-I mode once the Mediatek driver supports
  //                   switching to 1.8V.

  (void)card_supports_18v_signalling;
  // Try to switch the bus voltage to 1.8v
  // if (card_supports_18v_signalling) {
  //     st = sdmmc_do_command(sdmmc->host_zxdev, SDMMC_VOLTAGE_SWITCH, 0, setup_txn);
  //     if (st != ZX_OK) {
  //         zxlogf(ERROR, "sd: failed to send switch voltage command to card, "
  //                 "retcode = %d\n", st);
  //         goto err;
  //     }
  //
  //     st = sdmmc_set_signal_voltage(&sdmmc->host, SDMMC_VOLTAGE_180);
  //     if (st != ZX_OK) {
  //         zxlogf(ERROR, "sd: Card supports 1.8v signalling but was unable to "
  //                 "switch to 1.8v mode, retcode = %d\n", st);
  //         goto err;
  //     }
  // }

  if ((st = sdmmc_.MmcAllSendCid(raw_cid_)) != ZX_OK) {
    zxlogf(ERROR, "sd: ALL_SEND_CID failed with retcode = %d", st);
    return st;
  }

  uint16_t card_status;
  if ((st = sdmmc_.SdSendRelativeAddr(&card_status)) != ZX_OK) {
    zxlogf(ERROR, "sd: SEND_RELATIVE_ADDR failed with retcode = %d", st);
    return st;
  }

  if (card_status & 0xe000) {
    zxlogf(ERROR, "sd: SEND_RELATIVE_ADDR failed with resp = %d", (card_status & 0xe000));
    return ZX_ERR_INTERNAL;
  }
  if ((card_status & (1u << 8)) == 0) {
    zxlogf(ERROR, "sd: SEND_RELATIVE_ADDR failed. Card not ready.");
    return ZX_ERR_INTERNAL;
  }

  // Determine the size of the card.
  if ((st = sdmmc_.MmcSendCsd(raw_csd_)) != ZX_OK) {
    zxlogf(ERROR, "sd: failed to send app cmd, retcode = %d", st);
    return st;
  }

  // For now we only support SDHC cards. These cards must have a CSD type = 1,
  // since CSD type 0 is unable to support SDHC sized cards.
  const auto csd_structure = static_cast<uint8_t>((raw_csd_[15] >> 6) & 0x3);
  if (csd_structure != kCsdStructV2) {
    zxlogf(ERROR,
           "sd: unsupported card type, expected CSD version = %d, "
           "got version %d\n",
           kCsdStructV2, csd_structure);
    return ZX_ERR_INTERNAL;
  }

  const uint32_t c_size = (raw_csd_[6] | (raw_csd_[7] << 8) | (raw_csd_[8] << 16)) & 0x3f'ffff;
  block_info_.block_count = (c_size + 1ul) * 1024ul;
  block_info_.block_size = 512ul;
  zxlogf(INFO, "sd: found card with capacity = %" PRIu64 "B",
         block_info_.block_count * block_info_.block_size);

  if ((st = sdmmc_.SdSelectCard()) != ZX_OK) {
    zxlogf(ERROR, "sd: SELECT_CARD failed with retcode = %d", st);
    return st;
  }

  std::array<uint8_t, 8> scr;
  if ((st = sdmmc_.SdSendScr(scr)) != ZX_OK) {
    zxlogf(ERROR, "sd: SEND_SCR failed with retcode = %d", st);
    return st;
  }

  // TODO(bradenkell): Read SD_STATUS to see if the card supports discard (trim).

  // If this card supports 4 bit mode, then put it into 4 bit mode.
  const uint32_t supported_bus_widths = scr[1] & 0xf;
  if (supported_bus_widths & 0x4) {
    do {
      // First tell the card to go into four bit mode:
      if ((st = sdmmc_.SdSetBusWidth(SDMMC_BUS_WIDTH_FOUR)) != ZX_OK) {
        zxlogf(ERROR, "sd: failed to set card bus width, retcode = %d", st);
        break;
      }
      st = sdmmc_.host().SetBusWidth(SDMMC_BUS_WIDTH_FOUR);
      if (st != ZX_OK) {
        zxlogf(ERROR, "sd: failed to set host bus width, retcode = %d", st);
      }
    } while (false);
  }

  is_sd_ = true;
  return ZX_OK;
}

}  // namespace sdmmc
