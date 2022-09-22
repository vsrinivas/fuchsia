// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_SDIO_SDIO_BUS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_SDIO_SDIO_BUS_H_

#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <fuchsia/hardware/sdio/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/device.h>
#include <lib/zx/interrupt.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <type_traits>

#include <wlan/drivers/components/frame.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/bus_interface.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/moal_context.h"

namespace wlan::nxpfmac {

class SdioBus;

struct SdioContext : public MoalContext {
  SdioBus* bus_;
};

// SdioContext MUST NOT be polymorphic, we have to be able to cast to both SdioContext and
// MoalContext from a void pointer.
static_assert(!std::is_polymorphic_v<SdioContext>, "SdioContext must not be polymorphic");

class SdioBus : public BusInterface {
 public:
  ~SdioBus();
  static zx_status_t Create(zx_device_t* parent, mlan_device* mlan_dev,
                            std::unique_ptr<SdioBus>* out_sdio_bus);

  // BusInterface implementation
  zx_status_t TriggerMainProcess() override;
  zx_status_t OnMlanRegistered(void* mlan_adapter) override;
  zx_status_t OnFirmwareInitialized() override;
  zx_status_t PrepareVmo(uint8_t vmo_id, zx::vmo&& vmo, uint8_t* mapped_address,
                         size_t mapped_size) override;
  zx_status_t ReleaseVmo(uint8_t vmo_id) override;
  uint16_t GetRxHeadroom() const override;
  uint16_t GetTxHeadroom() const override;
  uint32_t GetBufferAlignment() const override;

 private:
  explicit SdioBus(zx_device_t* parent);
  SdioBus(const SdioBus&) = delete;
  SdioBus& operator=(const SdioBus&) = delete;

  zx_status_t Init(mlan_device* mlan_dev);

  zx_status_t StartIrqThread();
  void StopAndJoinIrqThread();
  void IrqThread();

  static mlan_status WriteRegister(t_void* pmoal, t_u32 reg, t_u32 data);
  static mlan_status ReadRegister(t_void* pmoal, t_u32 reg, t_u32* data);
  static mlan_status WriteDataSync(t_void* pmoal, pmlan_buffer pmbuf, t_u32 port, t_u32 timeout);
  static mlan_status ReadDataSync(t_void* pmoal, pmlan_buffer pmbuf, t_u32 port, t_u32 timeout);

  zx_status_t DoSyncRwTxn(pmlan_buffer pmbuf, t_u32 port, bool write);

  ddk::SdioProtocolClient func1_ __TA_GUARDED(func1_mutex_);
  std::mutex func1_mutex_;

  zx::interrupt interrupt_;

  thrd_t irq_thread_ = 0;
  std::atomic<bool> running_ = true;
  void* mlan_adapter_ = nullptr;
  SdioContext sdio_context_ = {};
  async::Loop main_process_loop_{&kAsyncLoopConfigNeverAttachToThread};
  std::atomic<bool> main_process_queued_{false};
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_SDIO_SDIO_BUS_H_
