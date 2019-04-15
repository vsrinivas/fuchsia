// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_PS_CFG_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_PS_CFG_H_

#include <wlan/mlme/ap/tim.h>

namespace wlan {

// Power Saving configuration managing TIM and DTIM.
class PsCfg {
 public:
  void SetDtimPeriod(uint8_t dtim_period) {
    // DTIM period of 0 is reserved.
    ZX_DEBUG_ASSERT(dtim_period > 0);

    dtim_period_ = dtim_period;
    dtim_count_ = dtim_period - 1;
  }

  uint8_t dtim_period() const { return dtim_period_; }

  uint8_t dtim_count() const { return dtim_count_; }

  TrafficIndicationMap* GetTim() { return &tim_; }

  const TrafficIndicationMap* GetTim() const { return &tim_; }

  uint8_t NextDtimCount() {
    if (IsDtim()) {
      dtim_count_ = dtim_period_ - 1;
      return dtim_count_;
    }
    return --dtim_count_;
  }

  uint8_t LastDtimCount() {
    if (dtim_count_ == dtim_period_ - 1) {
      return 0;
    }
    return dtim_count_ + 1;
  }

  bool IsDtim() const { return dtim_count_ == 0; }

 private:
  TrafficIndicationMap tim_;
  uint8_t dtim_period_ = 1;
  uint8_t dtim_count_ = 0;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_PS_CFG_H_
