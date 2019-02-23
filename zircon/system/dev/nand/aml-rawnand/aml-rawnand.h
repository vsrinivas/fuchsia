// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <utility>

#include <fbl/unique_ptr.h>

#include <string.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddktl/device.h>
#include <ddk/platform-defs.h>
#include <ddktl/pdev.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddktl/protocol/rawnand.h>

#include <ddk/io-buffer.h>
#include <ddk/mmio-buffer.h>
#include <ddk/protocol/platform/device.h>

#include <hw/reg.h>
#include <lib/sync/completion.h>
#include <soc/aml-common/aml-rawnand.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include "onfi.h"

namespace amlrawnand {

struct AmlController {
    int ecc_strength;
    int user_mode;
    int rand_mode;
#define NAND_USE_BOUNCE_BUFFER 0x1
    int options;
    int bch_mode;
};

class AmlRawNand;
using DeviceType = ddk::Device<AmlRawNand, ddk::Unbindable>;

class AmlRawNand : public DeviceType,
    public ddk::RawNandProtocol<AmlRawNand, ddk::base_protocol> {
public:
    explicit AmlRawNand(zx_device_t* parent, ddk::MmioBuffer mmio_nandreg,
                        ddk::MmioBuffer mmio_clockreg, zx::bti bti,
                        zx::interrupt irq) : DeviceType(parent),
        mmio_nandreg_(std::move(mmio_nandreg)),
        mmio_clockreg_(std::move(mmio_clockreg)),
        bti_(std::move(bti)),
        irq_(std::move(irq)) {}

    static zx_status_t Create(void* ctx, zx_device_t* parent);

    ~AmlRawNand() = default;

    void DdkRelease();
    void DdkUnbind();

    zx_status_t Bind();
    zx_status_t Init();
    zx_status_t RawNandReadPageHwecc(uint32_t nand_page, void* data, size_t data_size,
                                     size_t* data_actual, void* oob, size_t oob_size,
                                     size_t* oob_actual, uint32_t* ecc_correct);
    zx_status_t RawNandWritePageHwecc(const void* data, size_t data_size, const void* oob,
                                     size_t oob_size, uint32_t nand_page);
    zx_status_t RawNandEraseBlock(uint32_t nand_page);
    zx_status_t RawNandGetNandInfo(fuchsia_hardware_nand_Info* nand_info);
    void aml_cmd_ctrl(int32_t cmd, uint32_t ctrl);
    uint8_t aml_read_byte();

private:
    Onfi onfi_;
    void *info_buf_, *data_buf_;
    zx_paddr_t info_buf_paddr_, data_buf_paddr_;

    ddk::MmioBuffer mmio_nandreg_;
    ddk::MmioBuffer mmio_clockreg_;

    zx::bti bti_;
    zx::interrupt irq_;

    thrd_t irq_thread_;
    ddk::IoBuffer data_buffer_;
    ddk::IoBuffer info_buffer_;
    sync_completion_t req_completion_;

    AmlController controller_params_;
    uint32_t chip_select_;
    int chip_delay_;
    uint32_t writesize_; /* NAND pagesize - bytes */
    uint32_t erasesize_; /* size of erase block - bytes */
    uint32_t erasesize_pages_;
    uint32_t oobsize_; /* oob bytes per NAND page - bytes */
#define NAND_BUSWIDTH_16 0x00000002
    uint32_t bus_width_;  /* 16bit or 8bit ? */
    uint64_t chipsize_;   /* MiB */
    uint32_t page_shift_; /* NAND page shift */
    struct {
        uint64_t ecc_corrected;
        uint64_t failed;
    } stats;

    void nandctrl_set_cfg(uint32_t val);
    void nandctrl_set_timing_async(int bus_tim, int bus_cyc);
    void nandctrl_send_cmd(uint32_t cmd);
    void aml_cmd_idle(uint32_t time);
    zx_status_t aml_wait_cmd_finish(unsigned int timeout_ms);
    void aml_cmd_seed(uint32_t seed);
    void aml_cmd_n2m(uint32_t ecc_pages, uint32_t ecc_pagesize);
    void aml_cmd_m2n(uint32_t ecc_pages, uint32_t ecc_pagesize);
    void aml_cmd_m2n_page0();
    void aml_cmd_n2m_page0();
    zx_status_t aml_wait_dma_finish();
    void* aml_info_ptr(int i);
    zx_status_t aml_get_oob_byte(uint8_t* oob_buf);
    zx_status_t aml_set_oob_byte(const uint8_t* oob_buf, uint32_t ecc_pages);
    zx_status_t aml_get_ecc_corrections(int ecc_pages, uint32_t nand_page,
                                        uint32_t* ecc_corrected);
    zx_status_t aml_check_ecc_pages(int ecc_pages);
    zx_status_t aml_queue_rb();
    void aml_set_clock_rate(uint32_t clk_freq);
    void aml_clock_init();
    void aml_adjust_timings(uint32_t tRC_min, uint32_t tREA_max,
                            uint32_t RHOH_min);
    zx_status_t aml_get_flash_type();
    int IrqThread();
    void aml_set_encryption();
    zx_status_t aml_read_page0(void* data, size_t data_size, void* oob, size_t oob_size,
                               uint32_t nand_page, uint32_t* ecc_correct, int retries);
    zx_status_t aml_nand_init_from_page0();
    zx_status_t aml_raw_nand_allocbufs();
    zx_status_t aml_nand_init();
};

} // namespace amlrawnand
