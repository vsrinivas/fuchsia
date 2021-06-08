// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_NAND_DRIVERS_AML_RAWNAND_AML_RAWNAND_H_
#define SRC_DEVICES_NAND_DRIVERS_AML_RAWNAND_AML_RAWNAND_H_

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/rawnand/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/io-buffer.h>
#include <lib/ddk/mmio-buffer.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/time.h>
#include <string.h>
#include <unistd.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <ddktl/device.h>
#include <fbl/bits.h>
#include <fbl/mutex.h>
#include <soc/aml-common/aml-rawnand.h>

#include "src/devices/nand/drivers/aml-rawnand/onfi.h"

namespace amlrawnand {

struct AmlController {
  int ecc_strength;
  int user_mode;
  int rand_mode;
  int options;
  int bch_mode;
};

// In the case where user_mode == 2 (2 OOB bytes per ECC page),
// the controller adds one of these structs *per* ECC page in
// the info_buf.
struct AmlInfoFormat {
  uint16_t info_bytes;
  uint8_t zero_bits; /* bit0~5 is valid */
  union ecc_sta {
    uint8_t raw_value;
    fbl::BitFieldMember<uint8_t, 0, 6> eccerr_cnt;
    fbl::BitFieldMember<uint8_t, 7, 1> completed;
  } ecc;
  uint32_t reserved;

  // BitFieldMember is not trivially copyable so neither are we, have to copy
  // each member over manually (copy assignment is needed by tests).
  AmlInfoFormat& operator=(const AmlInfoFormat& other) {
    info_bytes = other.info_bytes;
    zero_bits = other.zero_bits;
    ecc.raw_value = other.ecc.raw_value;
    reserved = other.reserved;
    return *this;
  }
};

// gcc doesn't let us use __PACKED with fbl::BitFieldMember<>, but it shouldn't
// make a difference practically in how the AmlInfoFormat struct is laid out
// and this assertion will double-check that we don't need it.
static_assert(sizeof(AmlInfoFormat) == 8, "sizeof(AmlInfoFormat) must be exactly 8 bytes");

// This should always be the case, but we also need an array of AmlInfoFormats
// to have no padding between the items.
static_assert(sizeof(AmlInfoFormat[2]) == 16, "AmlInfoFormat has unexpected padding");

class AmlRawNand;
using DeviceType = ddk::Device<AmlRawNand, ddk::Unbindable, ddk::Suspendable>;

class AmlRawNand : public DeviceType, public ddk::RawNandProtocol<AmlRawNand, ddk::base_protocol> {
 public:
  explicit AmlRawNand(zx_device_t* parent, ddk::MmioBuffer mmio_nandreg,
                      ddk::MmioBuffer mmio_clockreg, zx::bti bti, zx::interrupt irq,
                      std::unique_ptr<Onfi> onfi)
      : DeviceType(parent),
        onfi_(std::move(onfi)),
        mmio_nandreg_(std::move(mmio_nandreg)),
        mmio_clockreg_(std::move(mmio_clockreg)),
        bti_(std::move(bti)),
        irq_(std::move(irq)) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  virtual ~AmlRawNand() = default;

  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkSuspend(ddk::SuspendTxn txn);

  zx_status_t Bind();
  zx_status_t Init();
  zx_status_t RawNandReadPageHwecc(uint32_t nand_page, uint8_t* data, size_t data_size,
                                   size_t* data_actual, uint8_t* oob, size_t oob_size,
                                   size_t* oob_actual, uint32_t* ecc_correct);
  zx_status_t RawNandWritePageHwecc(const uint8_t* data, size_t data_size, const uint8_t* oob,
                                    size_t oob_size, uint32_t nand_page);
  zx_status_t RawNandEraseBlock(uint32_t nand_page);
  zx_status_t RawNandGetNandInfo(nand_info_t* nand_info);

 protected:
  // These functions require complicated hardware interaction so need to be
  // overridden or called differently in tests.

  // Waits until a read completes.
  virtual zx_status_t AmlQueueRB();

  // Waits until DMA has transfered data into or out of the NAND buffers.
  virtual zx_status_t AmlWaitDmaFinish();

  // Reads a single status byte from a NAND register. Used during initialization
  // to query the chip information and settings.
  virtual uint8_t AmlReadByte();

  // Normally called when the driver is unregistered but is not automatically
  // called on destruction, so needs to be called manually by tests before
  // destroying this object.
  void CleanUpIrq();

  // Tests can fake page read/writes by copying bytes to/from these buffers.
  const ddk::IoBuffer& data_buffer() __TA_NO_THREAD_SAFETY_ANALYSIS {
    return buffers_->data_buffer;
  }
  const ddk::IoBuffer& info_buffer() __TA_NO_THREAD_SAFETY_ANALYSIS {
    return buffers_->info_buffer;
  }
  const zx::bti& bti() const { return bti_; }

 private:
  std::unique_ptr<Onfi> onfi_;

  struct Buffers {
    void *info_buf, *data_buf;
    zx_paddr_t info_buf_paddr, data_buf_paddr;
    ddk::IoBuffer data_buffer;
    ddk::IoBuffer info_buffer;
  };

  fbl::Mutex mutex_;
  std::optional<Buffers> buffers_ __TA_GUARDED(mutex_);
  ddk::MmioBuffer mmio_nandreg_;
  ddk::MmioBuffer mmio_clockreg_;

  zx::bti bti_;
  zx::interrupt irq_;

  AmlController controller_params_;
  uint32_t chip_select_ = 0;  // Default to 0.
  int chip_delay_ = 100;      // Conservative default before we query chip to find better value.
  uint32_t writesize_;        /* NAND pagesize - bytes */
  uint32_t erasesize_;        /* size of erase block - bytes */
  uint32_t erasesize_pages_;
  uint32_t oobsize_;    /* oob bytes per NAND page - bytes */
  uint32_t bus_width_;  /* 16bit or 8bit ? */
  uint64_t chipsize_;   /* MiB */
  uint32_t page_shift_; /* NAND page shift */
  struct {
    uint64_t ecc_corrected;
    uint64_t failed;
  } stats;

  polling_timings_t polling_timings_ = {};

  void AmlCmdCtrl(int32_t cmd, uint32_t ctrl);
  void NandctrlSetCfg(uint32_t val);
  void NandctrlSetTimingAsync(int bus_tim, int bus_cyc);
  void NandctrlSendCmd(uint32_t cmd);
  void AmlCmdIdle(uint32_t time);
  zx_status_t AmlWaitCmdFinish(zx::duration timeout, zx::duration first_interval,
                               zx::duration polling_interval);
  void AmlCmdSeed(uint32_t seed);
  void AmlCmdN2M(uint32_t ecc_pages, uint32_t ecc_pagesize);
  void AmlCmdM2N(uint32_t ecc_pages, uint32_t ecc_pagesize);
  void AmlCmdM2NPage0();
  void AmlCmdN2MPage0();
  // Returns the AmlInfoFormat struct corresponding to the i'th
  // ECC page. THIS ASSUMES user_mode == 2 (2 OOB bytes per ECC page).
  void* AmlInfoPtr(int i) __TA_REQUIRES(mutex_);
  zx_status_t AmlGetOOBByte(uint8_t* oob_buf, size_t* oob_actual) __TA_REQUIRES(mutex_);
  zx_status_t AmlSetOOBByte(const uint8_t* oob_buf, size_t oob_size, uint32_t ecc_pages)
      __TA_REQUIRES(mutex_);
  // Returns the maximum bitflips corrected on this NAND page
  // (the maximum bitflips across all of the ECC pages in this page).
  // erased will indicate whether the page was registered as an erased page, as
  // those should completely fail ECC.
  zx_status_t AmlGetECCCorrections(int ecc_pages, uint32_t nand_page, uint32_t* ecc_corrected,
                                   bool* erased) __TA_REQUIRES(mutex_);
  zx_status_t AmlCheckECCPages(int ecc_pages) __TA_REQUIRES(mutex_);
  void AmlSetClockRate(uint32_t clk_freq);
  void AmlClockInit();
  void AmlAdjustTimings(uint32_t tRC_min, uint32_t tREA_max, uint32_t RHOH_min);
  zx_status_t AmlGetFlashType();
  void AmlSetEncryption();
  zx_status_t AmlReadPage0(uint8_t* data, size_t data_size, uint8_t* oob, size_t oob_size,
                           uint32_t nand_page, uint32_t* ecc_correct, int retries);
  // Reads one of the page0 pages, and use the result to init
  // ECC algorithm and rand-mode.
  zx_status_t AmlNandInitFromPage0();
  zx_status_t AmlRawNandAllocBufs() __TA_REQUIRES(mutex_);
  zx_status_t AmlNandInit();
};

}  // namespace amlrawnand

#endif  // SRC_DEVICES_NAND_DRIVERS_AML_RAWNAND_AML_RAWNAND_H_
