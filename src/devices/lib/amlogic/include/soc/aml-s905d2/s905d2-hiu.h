// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_S905D2_HIU_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_S905D2_HIU_H_

#include <lib/ddk/hw/reg.h>
#include <lib/mmio/mmio-buffer.h>
#include <stdint.h>
#include <unistd.h>
#include <zircon/assert.h>

typedef enum {
  GP0_PLL = 0,
  PCIE_PLL,
  HIFI_PLL,
  SYS_PLL,
  SYS1_PLL,
  HIU_PLL_COUNT,
} hhi_plls_t;

typedef struct {
  uint64_t rate;
  uint32_t n;
  uint32_t m;
  uint32_t frac;
  uint32_t od;
} hhi_pll_rate_t;

typedef struct aml_hiu_dev {
  mmio_buffer_t mmio;
  MMIO_PTR uint8_t* regs_vaddr;
} aml_hiu_dev_t;

typedef struct aml_pll_dev {
  aml_hiu_dev_t* hiu;                // Pointer to the register control block.
  const hhi_pll_rate_t* rate_table;  // Pointer to this PLLs rate table.
  uint32_t rate_idx;                 // Index in rate table of current setting.
  uint32_t frequency;                // Current operating frequency.
  hhi_plls_t pll_num;                // Which pll is this
  size_t rate_count;                 // Number of entries in the rate table.
} aml_pll_dev_t;

__BEGIN_CDECLS

/*
    Maps the hiu register block (containing all the pll controls).
*/
zx_status_t s905d2_hiu_init(aml_hiu_dev_t* device);

/*
    Initializes the aml_hiu_dev_t struct assuming the register block is already
    mapped
*/
zx_status_t s905d2_hiu_init_etc(aml_hiu_dev_t* device, MMIO_PTR uint8_t* hiubase);

/*
    Initializes the selected pll. This resetting the pll and writing initial
    values to control registers.  When exiting init the PLL will be in a
    halted (de-enabled) state.
*/
zx_status_t s905d2_pll_init(aml_hiu_dev_t* device, aml_pll_dev_t* pll, hhi_plls_t pll_num);

/*
    Sets up the PLLs internal data structures without manipulating the hardware.
*/
void s905d2_pll_init_etc(aml_hiu_dev_t* device, aml_pll_dev_t* pll_dev, hhi_plls_t pll_num);

/*
    Sets the rate of the selected pll. If the requested frequency is not found
    it will return with ZX_ERR_NOT_SUPPORTED.
*/
zx_status_t s905d2_pll_set_rate(aml_pll_dev_t* pll, uint64_t freq);

/*
    Enables the selected pll.  This assumes the pll has been initialized and
    valid divider values have been written to the control registers.
*/
zx_status_t s905d2_pll_ena(aml_pll_dev_t* pll);

/*
    Disable the selected pll.  Returns if the pll was actually enabled
    when the call was made.
*/
bool s905d2_pll_disable(aml_pll_dev_t* pll_dev);

/*
    Look for freq in pll rate table.  Returns ZX_ERR_NOT_SUPPORTED if the rate
    can not be found.
*/
zx_status_t s905d2_pll_fetch_rate(aml_pll_dev_t* pll_dev, uint64_t freq,
                                  const hhi_pll_rate_t** pll_rate);

/*
    Returns correct pll rate table for selected pll.
*/
const hhi_pll_rate_t* s905d2_pll_get_rate_table(hhi_plls_t pll_num);

/*
    Returns the count of the rate table for the pll.
*/
size_t s905d2_get_rate_table_count(hhi_plls_t pll_num);

__END_CDECLS

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_S905D2_S905D2_HIU_H_
