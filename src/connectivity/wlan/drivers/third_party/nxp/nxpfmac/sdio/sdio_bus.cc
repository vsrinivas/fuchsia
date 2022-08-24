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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/sdio/sdio_bus.h"

#include <inttypes.h>
#include <lib/fit/defer.h>

#include <wlan/drivers/log.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/debug.h"

namespace wlan::nxpfmac {

constexpr char kSdioFunction1Name[] = "sdio-function-1";

namespace IrqPortKey {
enum IrqPortKey {
  Interrupt,
  Stop,
};
}  // namespace IrqPortKey

static zx_status_t get_card_type(uint32_t product_id, uint16_t* out_card_type) {
  switch (product_id) {
    case 0x9135:
      *out_card_type = CARD_TYPE_SD8887;
      return ZX_OK;
    case 0x9139:
      *out_card_type = CARD_TYPE_SD8801;
      return ZX_OK;
    case 0x912d:
      *out_card_type = CARD_TYPE_SD8897;
      return ZX_OK;
    case 0x9145:
      *out_card_type = CARD_TYPE_SD8977;
      return ZX_OK;
    case 0x9159:
      *out_card_type = CARD_TYPE_SD8978;
      return ZX_OK;
    case 0x9141:
      *out_card_type = CARD_TYPE_SD8997;
      return ZX_OK;
    case 0x9149:
      *out_card_type = CARD_TYPE_SD8987;
      return ZX_OK;
    default:
      break;
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t SdioBus::Create(zx_device_t* parent, mlan_device* mlan_dev,
                            std::unique_ptr<SdioBus>* out_sdio_bus) {
  if (out_sdio_bus == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::unique_ptr<SdioBus> bus(new SdioBus(parent));

  zx_status_t status = bus->Init(mlan_dev);
  if (status != ZX_OK) {
    lerror("Failed to initialize SDIO bus: %s", zx_status_get_string(status));
    return status;
  }

  *out_sdio_bus = std::move(bus);
  return ZX_OK;
}

SdioBus::SdioBus(zx_device_t* parent) : func1_(parent, kSdioFunction1Name) {}

SdioBus::~SdioBus() {
  if (func1_.is_valid()) {
    func1_.DisableFnIntr();
    func1_.DisableFn();
  }
  StopAndJoinIrqThread();
}

zx_status_t SdioBus::Init(mlan_device* mlan_dev) {
  {
    std::lock_guard lock(func1_mutex_);
    if (!func1_.is_valid()) {
      lerror("SDIO function 1 fragment not found");
      return ZX_ERR_NO_RESOURCES;
    }

    sdio_hw_info_t devinfo;
    zx_status_t status = func1_.GetDevHwInfo(&devinfo);
    if (status != ZX_OK) {
      NXPF_ERR("Unable to get SDIO hardware info: %s", zx_status_get_string(status));
      return status;
    }
    if (devinfo.dev_hw_info.num_funcs < 2) {
      lerror("Not enough SDIO funcs (need 2, have %u)", devinfo.dev_hw_info.num_funcs);
      return ZX_ERR_IO;
    }

    const uint32_t product_id = devinfo.funcs_hw_info[1].product_id;
    status = get_card_type(product_id, &mlan_dev->card_type);
    if (status != ZX_OK) {
      NXPF_ERR("Unable to determine card type for vid 0x%04x", product_id);
      return status;
    }

    status = func1_.EnableFn();
    if (status != ZX_OK) {
      lerror("Failed to enable SDIO func 1: %s", zx_status_get_string(status));
      return status;
    }

    status = func1_.GetInBandIntr(&interrupt_);
    if (status != ZX_OK) {
      NXPF_ERR("Failed to get SDIO interrupt: %s", zx_status_get_string(status));
      return status;
    }

    status = func1_.EnableFnIntr();
    if (status != ZX_OK) {
      NXPF_ERR("Failed to enable SDIO interrupt: %s", zx_status_get_string(status));
      return status;
    }

    status = func1_.UpdateBlockSize(MLAN_SDIO_BLOCK_SIZE, false);
    if (status != ZX_OK) {
      NXPF_ERR("Failed to set SDIO block size: %s", zx_status_get_string(status));
      return status;
    }
  }

  // Populate bus specific callbacks.
  mlan_dev->callbacks.moal_write_reg = &SdioBus::WriteRegister;
  mlan_dev->callbacks.moal_read_reg = &SdioBus::ReadRegister;
  mlan_dev->callbacks.moal_write_data_sync = &SdioBus::WriteDataSync;
  mlan_dev->callbacks.moal_read_data_sync = &SdioBus::ReadDataSync;

  // Populate moal handle
  sdio_context_.bus_ = this;
  mlan_dev->pmoal_handle = &sdio_context_;

  // Use a port for IRQ signaling, this makes it easy to send other messages to the IRQ thread.
  zx_status_t status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &irq_port_);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to create IRQ port: %s", zx_status_get_string(status));
    return status;
  }
  status = interrupt_.bind(irq_port_, IrqPortKey::Interrupt, ZX_INTERRUPT_BIND);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to bind interrupt to port: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t SdioBus::OnMlanRegistered(void* mlan_adapter) {
  mlan_adapter_ = mlan_adapter;
  return ZX_OK;
}

zx_status_t SdioBus::OnFirmwareInitialized() { return StartIrqThread(); }

zx_status_t SdioBus::StartIrqThread() {
  if (irq_thread_) {
    NXPF_ERR("IRQ thread is already running");
    return ZX_ERR_ALREADY_EXISTS;
  }
  thrd_start_t start = [](void* context) {
    static_cast<SdioBus*>(context)->IrqThread();
    return 0;
  };
  int result = thrd_create_with_name(&irq_thread_, start, this, "nxpfmac_irq_thread");
  if (result != thrd_success) {
    NXPF_ERR("Could not create IRQ thread: %d", result);
    return ZX_ERR_NO_RESOURCES;
  }
  return ZX_OK;
}

void SdioBus::StopAndJoinIrqThread() {
  if (irq_port_.is_valid()) {
    // Tell the IRQ thread to shut down.
    zx_port_packet_t packet = {
        .key = IrqPortKey::Stop,
    };
    running_ = false;
    irq_port_.queue(&packet);
    if (irq_thread_) {
      // If the thread is running it should finish after the shutdown message, wait for it.
      int result = 0;
      thrd_join(irq_thread_, &result);
      irq_thread_ = 0;
    }
  }
}

void SdioBus::IrqThread() {
  NXPF_INFO("IRQ thread started");

  while (running_.load(std::memory_order_relaxed)) {
    zx_port_packet_t packet;
    zx_status_t status = irq_port_.wait(zx::time::infinite(), &packet);
    if (status != ZX_OK) {
      NXPF_ERR("Error waiting on IRQ port: %s", zx_status_get_string(status));
      break;
    }
    if (packet.key == IrqPortKey::Stop) {
      NXPF_INFO("Received request to stop IRQ thread");
      break;
    }
    if (packet.key != IrqPortKey::Interrupt) {
      NXPF_WARN("Unknown IRQ port key: %" PRIu64, packet.key);
      continue;
    }

    // At this point we know it's an interrupt, make sure to ack it no matter what.
    fit::deferred_action ack_irq([&] { interrupt_.ack(); });

    mlan_status ml_status = mlan_interrupt(0, mlan_adapter_);
    if (ml_status != MLAN_STATUS_SUCCESS) {
      // Don't fail because of this, other platform drivers don't.
      NXPF_WARN("mlan_interrupt failed: %d", ml_status);
    }

    ml_status = mlan_main_process(mlan_adapter_);
    if (ml_status != MLAN_STATUS_SUCCESS) {
      // Don't fail because of this, other platform drivers don't.
      NXPF_WARN("mlan_main_process failed: %d", ml_status);
    }
  }
  NXPF_INFO("IRQ thread exiting");
}

mlan_status SdioBus::WriteRegister(t_void* pmoal, t_u32 reg, t_u32 data) {
  SdioBus* bus = static_cast<SdioContext*>(pmoal)->bus_;

  std::lock_guard lock(bus->func1_mutex_);
  // The Banjo SDIO mocks used for unit tests can't handle a nullptr as the last argument for
  // DoRwByte, even if it's not used for writes. Provide an unused pointer to avoid this problem.
  uint8_t unused = 0;
  zx_status_t status = bus->func1_.DoRwByte(true, reg, static_cast<uint8_t>(data), &unused);
  return status == ZX_OK ? MLAN_STATUS_SUCCESS : MLAN_STATUS_FAILURE;
}

mlan_status SdioBus::ReadRegister(t_void* pmoal, t_u32 reg, t_u32* data) {
  SdioBus* bus = static_cast<SdioContext*>(pmoal)->bus_;

  std::lock_guard lock(bus->func1_mutex_);
  uint8_t value = 0;
  zx_status_t status = bus->func1_.DoRwByte(false, reg, 0u, &value);
  if (status == ZX_OK) {
    *data = value;
  }
  return status == ZX_OK ? MLAN_STATUS_SUCCESS : MLAN_STATUS_FAILURE;
}

mlan_status SdioBus::WriteDataSync(t_void* pmoal, pmlan_buffer pmbuf, t_u32 port, t_u32 timeout) {
  SdioBus* bus = static_cast<SdioContext*>(pmoal)->bus_;

  zx_status_t status = bus->DoSyncRwTxn(pmbuf, port, true);
  return status == ZX_OK ? MLAN_STATUS_SUCCESS : MLAN_STATUS_FAILURE;
}

mlan_status SdioBus::ReadDataSync(t_void* pmoal, pmlan_buffer pmbuf, t_u32 port, t_u32 timeout) {
  SdioBus* bus = static_cast<SdioContext*>(pmoal)->bus_;

  zx_status_t status = bus->DoSyncRwTxn(pmbuf, port, false);
  return status == ZX_OK ? MLAN_STATUS_SUCCESS : MLAN_STATUS_FAILURE;
}

zx_status_t SdioBus::DoSyncRwTxn(pmlan_buffer pmbuf, t_u32 port, bool write) {
  if (pmbuf->use_count > 1) {
    NXPF_ERR("Requires scatter/gather, not yet implemented");
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint8_t* buffer = pmbuf->pbuf + pmbuf->data_offset;
  const bool use_block_mode = (port & MLAN_SDIO_BYTE_MODE_MASK) == 0;
  const uint32_t block_size = use_block_mode ? MLAN_SDIO_BLOCK_SIZE : 1;
  // This will drop trailing data if data_len is not a multiple of the block size. But this seems
  // to be the expectation in mlan. Leave as is until we know otherwise.
  const uint32_t block_count =
      use_block_mode ? (pmbuf->data_len / MLAN_SDIO_BLOCK_SIZE) : pmbuf->data_len;
  const uint32_t transfer_size = block_count * block_size;
  uint32_t io_port = (port & MLAN_SDIO_IO_PORT_MASK);

  sdio_rw_txn_t txn{
      .addr = io_port,
      .data_size = transfer_size,
      .incr = false,
      .write = write,
      .use_dma = false,
      .virt_buffer = buffer,
      .virt_size = transfer_size,
      .buf_offset = 0,
  };

  std::lock_guard lock(func1_mutex_);
  zx_status_t status = func1_.DoRwTxn(&txn);
  if (status != ZX_OK) {
    NXPF_ERR("SDIO %s failed: %s", write ? "write" : "read", zx_status_get_string(status));
    zx_status_t abort_status = func1_.IoAbort();
    if (abort_status != ZX_OK) {
      NXPF_ERR("SDIO IO abort failed: %s", zx_status_get_string(abort_status));
    }
    return status;
  }

  return ZX_OK;
}

}  // namespace wlan::nxpfmac
