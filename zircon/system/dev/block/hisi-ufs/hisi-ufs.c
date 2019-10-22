// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/platform-device.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>

#include "ufs.h"

typedef struct {
  uint32_t addr;
  uint32_t val;
} ufs_cfg_attr_t;

static ufs_cfg_attr_t hi3660_ufs_calib_of_rateb[] = {
    {0xD0C10000, 0x1},  /* Unipro VS_Mphy_disable */
    {0x156A0000, 0x2},  /* PA_HSSeries */
    {0x81140000, 0x1},  /* MPHY CBRATESEL */
    {0x81210000, 0x2D}, /* MPHY CBOVRCTRL2 */
    {0x81220000, 0x1},  /* MPHY CBOVRCTRL3 */
    {0xD0850000, 0x1},  /* Unipro VS_MphyCfgUpdt */
    {0x800D0004, 0x58}, /* MPHY RXOVRCTRL4 rx0 */
    {0x800D0005, 0x58}, /* MPHY RXOVRCTRL4 rx1 */
    {0x800E0004, 0xB},  /* MPHY RXOVRCTRL5 rx0 */
    {0x800E0005, 0xB},  /* MPHY RXOVRCTRL5 rx1 */
    {0x80090004, 0x1},  /* MPHY RXSQCTRL rx0 */
    {0x80090005, 0x1},  /* MPHY RXSQCTRL rx1 */
    {0xD0850000, 0x1},  /* Unipro VS_MphyCfgUpdt */
    {0, 0},
};

static ufs_cfg_attr_t hi3660_ufs_prelink_calib_attr[] = {
    {0x81130000, 0x1},
    {0xD0850000, 0x1},
    {0x008F0004, 0x7},  /* RX Min Activate Time */
    {0x008F0005, 0x7},  /* RX Min Activate Time */
    {0x00950004, 0x4F}, /* Gear3 Synclength */
    {0x00950005, 0x4F}, /* Gear3 Synclength */
    {0x00940004, 0x4F}, /* Gear2 Synclength */
    {0x00940005, 0x4F}, /* Gear2 Synclength */
    {0x008B0004, 0x4F}, /* Gear1 Synclength */
    {0x008B0005, 0x4F}, /* Gear1 Synclength */
    {0x000F0000, 0x5},  /* Thibernate Tx */
    {0x000F0001, 0x5},  /* Thibernate Tx */
    {0xD0850000, 0x1},  /* Unipro VS_MphyCfgUpdt */
    {0, 0},
};

static ufs_cfg_attr_t hi3660_ufs_postlink_calib_attr[] = {
    {0x20440000, 0x0}, /* Unipro DL_AFC0 CreditThreshold */
    {0x20450000, 0x0}, /* Unipro DL_TC0 OutAckThreshold */
    {0x20400000, 0x9}, /* Unipro DL_TC0TXFC Threshold */
    {0, 0},
};

static void mphy_hi3660_attr_write(volatile void* regs, uint16_t addr, uint16_t val) {
  ufshc_send_uic_command(regs, DME_SET, MPHY_ATTR_DEMPH_ADDR_MSB, (addr & 0xFF00) >> 8);
  ufshc_send_uic_command(regs, DME_SET, MPHY_ATTR_DEMPH_ADDR_LSB, (addr & 0xFF));
  ufshc_send_uic_command(regs, DME_SET, MPHY_ATTR_DEMPH_VAL_MSB, (val & 0xFF00) >> 8);
  ufshc_send_uic_command(regs, DME_SET, MPHY_ATTR_DEMPH_VAL_LSB, (val & 0xFF));
  ufshc_send_uic_command(regs, DME_SET, MPHY_ATTR_DEMPH_CTRL, 1);
}

static void mphy_hi3660_config_equalizer(volatile void* regs) {
  mphy_hi3660_attr_write(regs, MPHY_ATTR_DEMPH_ADDR1, MPHY_ATTR_DEMPH_VAL1);
  mphy_hi3660_attr_write(regs, MPHY_ATTR_DEMPH_ADDR2, MPHY_ATTR_DEMPH_VAL1);
  mphy_hi3660_attr_write(regs, MPHY_ATTR_DEMPH_ADDR3, MPHY_ATTR_DEMPH_VAL2);
  mphy_hi3660_attr_write(regs, MPHY_ATTR_DEMPH_ADDR4, MPHY_ATTR_DEMPH_VAL2);
}

static zx_status_t mphy_hi3660_pre_link_startup(volatile void* regs) {
  zx_status_t status;
  uint32_t reg_val;

  ufshc_check_h8(regs);

  // When chip status is normal
  writel(UFS_HCLKDIV_NORMAL_VAL, regs + REG_UFS_HCLKDIV_OFF);

  ufshc_disable_auto_h8(regs);

  // Unipro PA Local TX LCC Enable
  status = ufshc_send_uic_command(regs, DME_SET, UPRO_PA_TX_LCC_CTRL, 0x0);
  if (status != ZX_OK)
    return status;

  // Close Unipro VS Mk2 Extn support
  status = ufshc_send_uic_command(regs, DME_SET, UPRO_MK2_EXTN_SUP, 0x0);
  if (status != ZX_OK)
    return status;

  reg_val = ufshc_uic_cmd_read(regs, DME_GET, UPRO_MK2_EXTN_SUP);
  if (0 != reg_val) {
    UFS_ERROR("Unipro Mk2 close failed!\n");
    return ZX_ERR_BAD_STATE;
  }

  mphy_hi3660_config_equalizer(regs);
  return ZX_OK;
}

static zx_status_t ufs_hi3660_calibrate(volatile void* regs, ufs_cfg_attr_t* cfg) {
  uint32_t i;
  zx_status_t status = ZX_OK;

  for (i = 0; cfg[i].addr; i++) {
    status = ufshc_send_uic_command(regs, DME_SET, cfg[i].addr, cfg[i].val);
    if (status != ZX_OK) {
      UFS_ERROR("UFS calib Fail! index=%u\n", i);
      return status;
    }
  }

  return status;
}

static zx_status_t ufs_hi3660_pre_link_startup(volatile void* regs) {
  zx_status_t status;
  ufs_cfg_attr_t* cfg = hi3660_ufs_calib_of_rateb;

  status = ufs_hi3660_calibrate(regs, cfg);
  if (status != ZX_OK)
    return status;

  cfg = hi3660_ufs_prelink_calib_attr;
  status = ufs_hi3660_calibrate(regs, cfg);
  if (status != ZX_OK)
    return status;

  status = mphy_hi3660_pre_link_startup(regs);
  return status;
}

static zx_status_t ufs_hi3660_post_link_startup(volatile void* regs) {
  zx_status_t status = ZX_OK;
  uint32_t i = 0;

  for (; hi3660_ufs_postlink_calib_attr[i].addr; i++) {
    status = ufshc_send_uic_command(regs, DME_SET, hi3660_ufs_postlink_calib_attr[i].addr,
                                    hi3660_ufs_postlink_calib_attr[i].val);
    if (status != ZX_OK)
      UFS_ERROR("hi3660_ufs_postlink_calib_attr index=%u Fail!\n", i);
  }

  return status;
}

static zx_status_t ufs_hi3660_link_startup(volatile void* regs, uint8_t status) {
  zx_status_t ret = 0;

  switch (status) {
    case PRE_CHANGE:
      ret = ufs_hi3660_pre_link_startup(regs);
      break;
    case POST_CHANGE:
      ret = ufs_hi3660_post_link_startup(regs);
      break;
    default:
      break;
  }

  return ret;
}

static ufs_hba_variant_ops_t ufs_hi3660_vops = {
    .name = "hi3660_ufs",
    .link_startup = ufs_hi3660_link_startup,
};

static void hisi_ufs_release(void* ctx) {
  ufshc_dev_t* dev = ctx;
  mmio_buffer_release(&dev->ufshc_mmio);
  zx_handle_close(dev->bti);
  free(dev);
}

static zx_protocol_device_t hisi_ufs_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = hisi_ufs_release,
};

static zx_status_t hisi_ufs_bind(void* ctx, zx_device_t* parent) {
  ufshc_dev_t* dev;
  zx_status_t status;

  zxlogf(INFO, "hisi_ufs_bind\n");

  dev = calloc(1, sizeof(ufshc_dev_t));
  if (!dev) {
    UFS_ERROR("Out of memory!\n");
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &dev->pdev)) != ZX_OK) {
    UFS_ERROR("ZX_PROTOCOL_PDEV not available!\n");
    goto fail;
  }

  status = pdev_get_bti(&dev->pdev, 0, &dev->bti);
  if (status != ZX_OK) {
    UFS_ERROR("pdev_get_bti failed!\n");
    goto fail;
  }

  status = pdev_map_mmio_buffer(&dev->pdev, MMIO_UFSHC, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                &dev->ufshc_mmio);
  if (status != ZX_OK) {
    UFS_ERROR("pdev_map_mmio_buffer ufshc mmio failed!:%d\n", status);
    goto fail;
  }

  status = ufshc_init(dev, &ufs_hi3660_vops);
  if (status != ZX_OK) {
    UFS_ERROR("UFS HC enabling failed!status=%d\n", status);
    goto fail;
  }
  UFS_DBG("UFS HC Initialization Success.\n");

  // Create the device.
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "hisi-ufs",
      .ops = &hisi_ufs_device_proto,
      .ctx = dev,
      .flags = DEVICE_ADD_NON_BINDABLE,
  };

  status = device_add(parent, &args, &dev->zxdev);
  if (status != ZX_OK) {
    UFS_ERROR("hisi UFS device pdev_add status: %d\n", status);
    goto fail;
  }

  status = ufs_create_worker_thread(dev);
  if (status != ZX_OK) {
    UFS_ERROR("UFS worker_thread create fail! status:%d\n", status);
    device_async_remove(dev->zxdev);
    return status;
  }

  return ZX_OK;

fail:
  hisi_ufs_release(dev);
  return status;
}

static zx_driver_ops_t hisi_ufs_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = hisi_ufs_bind,
};

ZIRCON_DRIVER_BEGIN(hisi_ufs, hisi_ufs_driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_96BOARDS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_HIKEY960),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_HISILICON_UFS), ZIRCON_DRIVER_END(hisi_ufs)
