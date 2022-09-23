// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_DPLL_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_DPLL_H_

#include <assert.h>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"

namespace tgl_registers {

// DPLL_CTRL1
class DpllControl1 : public hwreg::RegisterBase<DpllControl1, uint32_t> {
 public:
  enum class LinkRate : uint8_t {
    k2700Mhz = 0,  // DisplayPort 5.4 GHz  (VCO 8100)
    k1350Mhz = 1,  // DisplayPort 2.7 GHz  (VCO 8100)
    k810Mhz = 2,   // DisplayPort 1.62 GHz (VCO 8100)
    k1620Mhz = 3,  // DisplayPort 3.24 GHz (VCO 8100)
    k1080Mhz = 4,  // DisplayPort 2.16 GHz (VCO 8640)
    k2160Mhz = 5,  // DisplayPort 4.32 GHz (VCO 8640)
  };

  hwreg::BitfieldRef<uint32_t> dpll_hdmi_mode(Dpll dpll) {
    int bit = dpll * 6 + 5;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> dpll_ssc_enable(Dpll dpll) {
    int bit = dpll * 6 + 4;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> dpll_link_rate(Dpll dpll) {
    int bit = dpll * 6 + 1;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 2, bit);
  }

  LinkRate GetLinkRate(Dpll dpll) { return static_cast<LinkRate>(dpll_link_rate(dpll).get()); }

  void SetLinkRate(Dpll dpll, LinkRate rate) {
    dpll_link_rate(dpll).set(static_cast<std::underlying_type_t<LinkRate>>(rate));
  }

  hwreg::BitfieldRef<uint32_t> dpll_override(Dpll dpll) {
    int bit = dpll * 6;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get() { return hwreg::RegisterAddr<DpllControl1>(0x6c058); }
};

// DPLL_CTRL2
class DpllControl2 : public hwreg::RegisterBase<DpllControl2, uint32_t> {
 public:
  hwreg::BitfieldRef<uint32_t> ddi_clock_off(Ddi ddi) {
    int bit = 15 + ddi;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> ddi_clock_select(Ddi ddi) {
    int bit = ddi * 3 + 1;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit + 1, bit);
  }

  hwreg::BitfieldRef<uint32_t> ddi_select_override(Ddi ddi) {
    int bit = ddi * 3;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get() { return hwreg::RegisterAddr<DpllControl2>(0x6c05c); }
};

// DPLL_CFGCR1
class DpllConfig1 : public hwreg::RegisterBase<DpllConfig1, uint32_t> {
 public:
  DEF_BIT(31, frequency_enable);
  DEF_FIELD(23, 9, dco_fraction);
  DEF_FIELD(8, 0, dco_integer);

  static auto Get(Dpll dpll) {
    ZX_ASSERT(dpll == DPLL_1 || dpll == DPLL_2 || dpll == DPLL_3);
    return hwreg::RegisterAddr<DpllConfig1>(0x6c040 + ((dpll - 1) * 8));
  }
};

// DPLL_CFGCR2
class DpllConfig2 : public hwreg::RegisterBase<DpllConfig2, uint32_t> {
 public:
  DEF_FIELD(15, 8, qdiv_ratio);
  DEF_BIT(7, qdiv_mode);

  DEF_FIELD(6, 5, kdiv_ratio);
  static constexpr uint8_t kKdiv5 = 0;
  static constexpr uint8_t kKdiv2 = 1;
  static constexpr uint8_t kKdiv3 = 2;
  static constexpr uint8_t kKdiv1 = 3;

  DEF_FIELD(4, 2, pdiv_ratio);
  static constexpr uint8_t kPdiv1 = 0;
  static constexpr uint8_t kPdiv2 = 1;
  static constexpr uint8_t kPdiv3 = 2;
  static constexpr uint8_t kPdiv7 = 4;

  DEF_FIELD(1, 0, central_freq);
  static constexpr uint8_t k9600Mhz = 0;
  static constexpr uint8_t k9000Mhz = 1;
  static constexpr uint8_t k8400Mhz = 3;

  static auto Get(int dpll) {
    ZX_ASSERT(dpll == DPLL_1 || dpll == DPLL_2 || dpll == DPLL_3);
    return hwreg::RegisterAddr<DpllConfig2>(0x6c044 + ((dpll - 1) * 8));
  }
};

// DPLL_ENABLE
// These registers are used to enable the PLLs for driving the ports.
//
// On Tiger Lake, these registers are defined as a single "DPLL_ENABLE"
// register.
//
// On Skylake / Kaby Lake, there is no single "DPLL_ENABLE" register, but we
// unify enablement registers for 4 different DPLLs into this "Virtual
// register".
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 656-657
// Kaby Lake:
// - LCPLL1_CTL: IHD-OS-KBL-Vol 2c-1.17 Part 1, Page 1121
// - LCPLL2_CTL: IHD-OS-KBL-Vol 2c-1.17 Part 1, Page 1122
// - WRPLL_CTL1 / WRPLL_CTL2: IHD-OS-KBL-Vol 2c-1.17 Part 2, Page 1349-1350
class DpllEnable : public hwreg::RegisterBase<DpllEnable, uint32_t> {
 public:
  DEF_BIT(31, enable_dpll);

  DEF_BIT(30, pll_is_locked);

  // This bit is for Tiger Lake only.
  DEF_BIT(27, power_enable_request_tiger_lake);

  // This bit is for Tiger Lake only.
  DEF_BIT(26, power_is_enabled_tiger_lake);

  static auto GetForSkylakeDpll(Dpll dpll) {
    switch (dpll) {
      case DPLL_0:
        return hwreg::RegisterAddr<DpllEnable>(0x46010);  // LCPLL1_CTL
      case DPLL_1:
        return hwreg::RegisterAddr<DpllEnable>(0x46014);  // LCPLL2_CTL
      case DPLL_2:
        return hwreg::RegisterAddr<DpllEnable>(0x46040);  // WRPLL_CTL1
      case DPLL_3:
        return hwreg::RegisterAddr<DpllEnable>(0x46060);  // WRPLL_CTL2
      default:
        ZX_DEBUG_ASSERT_MSG(false, "Invalid DPLL (%d)", dpll);
        return hwreg::RegisterAddr<DpllEnable>(0x0);
    }
  }

  static auto GetForTigerLakeDpll(Dpll dpll) {
    switch (dpll) {
      // TODO(fxbug.dev/105240): Add DPLL4.
      case DPLL_0:
        return hwreg::RegisterAddr<DpllEnable>(0x46010);  // DPLL0_ENABLE
      case DPLL_1:
        return hwreg::RegisterAddr<DpllEnable>(0x46014);  // DPLL1_ENABLE
      case DPLL_2:
        return hwreg::RegisterAddr<DpllEnable>(0x46020);  // TBT_PLL_ENABLE
      // Tiger Lake: On IHD-OS-TGL-Vol 2c-1.22-Rev 2.0, Page 656, it mentions
      // that the MG register instances are used for Type-C in general, so they
      // can control Dekel PLLs as well (for example, MGPLL1_ENABLE controls
      // Dekel PLL Type-C Port 1).
      case DPLL_TC_1:
        return hwreg::RegisterAddr<DpllEnable>(0x46030);  // MGPLL1_ENABLE
      case DPLL_TC_2:
        return hwreg::RegisterAddr<DpllEnable>(0x46034);  // MGPLL2_ENABLE
      case DPLL_TC_3:
        return hwreg::RegisterAddr<DpllEnable>(0x46038);  // MGPLL3_ENABLE
      case DPLL_TC_4:
        return hwreg::RegisterAddr<DpllEnable>(0x4603C);  // MGPLL4_ENABLE
      case DPLL_TC_5:
        return hwreg::RegisterAddr<DpllEnable>(0x46040);  // MGPLL5_ENABLE
      case DPLL_TC_6:
        return hwreg::RegisterAddr<DpllEnable>(0x46044);  // MGPLL6_ENABLE
      default:
        ZX_ASSERT_MSG(false, "Invalid DPLL (%d)", dpll);
    }
  }
};

// DPLL_STATUS
class DpllStatus : public hwreg::RegisterBase<DpllStatus, uint32_t> {
 public:
  hwreg::BitfieldRef<uint32_t> dpll_lock(Dpll dpll) {
    int bit = dpll * 8;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get() { return hwreg::RegisterAddr<DpllStatus>(0x6c060); }
};

// LCPLL1_CTL
class Lcpll1Control : public hwreg::RegisterBase<Lcpll1Control, uint32_t> {
 public:
  DEF_BIT(30, pll_lock);

  static auto Get() { return hwreg::RegisterAddr<Lcpll1Control>(0x46010); }
};

}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_DPLL_H_
