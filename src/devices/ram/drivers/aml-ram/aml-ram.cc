// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-ram.h"

#include <lib/device-protocol/pdev.h>
#include <zircon/assert.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>
#include <soc/aml-s905d2/s905d2-hw.h>

namespace amlogic_ram {
// There are 4 monitoring channels and each one can agregate up to 64
// hardware memory ports. NOTE: the word channel and port in this file
// refer to hardware, not to zircon objects.
constexpr size_t MEMBW_MAX_CHANNELS = 4u;

// Controls start,stop and if polling or interrupt mode.
constexpr uint32_t MEMBW_PORTS_CTRL = (0x0020 << 2);
constexpr uint32_t DMC_QOS_ENABLE_CTRL = (0x01 << 31);
constexpr uint32_t DMC_QOS_CLEAR_CTRL = (0x01 << 30);

// Returns the granted cycles counters total.
constexpr uint32_t MEMBW_ALL_GRANT_CNT = (0x002a << 2);
// Returns the granted cycles per channel.
constexpr uint32_t MEMBW_C0_GRANT_CNT = (0x2b << 2);
constexpr uint32_t MEMBW_C1_GRANT_CNT = (0x2c << 2);
constexpr uint32_t MEMBW_C2_GRANT_CNT = (0x2d << 2);
constexpr uint32_t MEMBW_C3_GRANT_CNT = (0x2e << 2);

// Controls how long to measure cycles for.
constexpr uint32_t MEMBW_TIMER = (0x002f << 2);

// Controls which ports are assigned to each channel.
constexpr uint32_t MEMBW_RP[MEMBW_MAX_CHANNELS] = {(0x0021 << 2), (0x0023 << 2), (0x0025 << 2),
                                                   (0x0027 << 2)};

// Controls wich subports are assinged to each channel.
constexpr uint32_t MEMBW_SP[MEMBW_MAX_CHANNELS] = {(0x0022 << 2), (0x0024 << 2), (0x0026 << 2),
                                                   (0x0028 << 2)};

constexpr double kMemCyclePerSecond = (912.0 / 2.0);
constexpr uint32_t kMemCycleCount = 1024 * 1024 * 57u;

// Ports constants for Astro and Sherlock.
// TODO(cpu) move this constants out of the driver and into the client.
#define PortID_ARM_AE (0x01u << 0)
#define PortID_MALI (0x01u << 1)
#define PortID_PCIE (0x01u << 2)
#define PortID_HDCP (0x01u << 3)
#define PortID_HEVC_FRONT (0x01u << 4)
#define PortID_TEST (0x01u << 5)
#define PortID_USB30 (0x01u << 6)
#define PortID_HEVC_BACK (0x01u << 8)
#define PortID_H265_ENC (0x01u << 9)
#define PortID_VPU_R1 (0x01u << 16)
#define PortID_VPU_R2 (0x01u << 17)
#define PortID_VPU_R3 (0x01u << 18)
#define PortID_VPU_W1 (0x01u << 19)
#define PortID_VPU_W2 (0x01u << 20)
#define PortID_VDEC (0x01u << 21)
#define PortID_HCODEC (0x01u << 22)
#define PortID_GE2D (0x01u << 23)
// Sherlock-only ports.
#define PortID_NNA (0x01u << 10)
#define PortID_GDC (0x01u << 11)
#define PortID_MIPI_ISP (0x01u << 12)
#define PortID_ARM_AF (0x01u << 13)

struct BwPorts {
  // Each port is a single bit per channel.
  uint64_t chn[MEMBW_MAX_CHANNELS];
};

constexpr double CalcBandwidth(uint32_t counter) {
  return (static_cast<double>(counter) * 16.0 * kMemCyclePerSecond) / kMemCycleCount;
}

void InitBandwithTimer(ddk::MmioBuffer& mmio) {
  // The only thing to init is how many cycles to sample for
  // bandwith calculations.
  mmio.Write32(kMemCycleCount, MEMBW_TIMER);
}

// Read the counters and output the bandwith computation via zxlog.
// This only happens if the driver has driver.aml_ram.log=+trace.
zx_status_t ReadBandwithCounters(ddk::MmioBuffer& mmio, const BwPorts& ports, zx::event& event) {
  uint32_t channels_enabled = 0u;
  for (size_t ix = 0; ix != MEMBW_MAX_CHANNELS; ++ix) {
    if (ports.chn[ix] > 0xffffffff) {
      // We don't support sub-ports (bits above 31) yet.
      return ZX_ERR_NOT_SUPPORTED;
    }
    channels_enabled |= (ports.chn[ix] != 0) ? (1u << ix) : 0;
    mmio.Write32(static_cast<uint32_t>(ports.chn[ix]), MEMBW_RP[ix]);
    mmio.Write32(0xffff, MEMBW_SP[ix]);
  }

  if (channels_enabled == 0x0) {
    // Nothing to monitor.
    return ZX_OK;
  }

  mmio.Write32(channels_enabled | DMC_QOS_ENABLE_CTRL, MEMBW_PORTS_CTRL);

  uint32_t value = mmio.Read32(MEMBW_PORTS_CTRL);
  uint32_t count = 0;

  // Polling loop for the bandwith cycle counters.
  // TODO(cpu): tune wait interval to minimize polling.
  while (value & DMC_QOS_ENABLE_CTRL) {
    if (count++ > 20) {
      return ZX_ERR_TIMED_OUT;
    }

    auto status = event.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::msec(50)), nullptr);
    if (status != ZX_ERR_TIMED_OUT) {
      // Likely shutdown. The caller handles this.
      return ZX_OK;
    }
    value = mmio.Read32(MEMBW_PORTS_CTRL);
  }

  auto counter_all = mmio.Read32(MEMBW_ALL_GRANT_CNT);
  auto counter_0 = mmio.Read32(MEMBW_C0_GRANT_CNT);
  auto counter_1 = mmio.Read32(MEMBW_C1_GRANT_CNT);
  auto counter_2 = mmio.Read32(MEMBW_C2_GRANT_CNT);
  auto counter_3 = mmio.Read32(MEMBW_C3_GRANT_CNT);

  zxlogf(TRACE, "aml-ram: bw:%g %g %g %g %g cz:%d\n", CalcBandwidth(counter_all),
         CalcBandwidth(counter_0), CalcBandwidth(counter_1), CalcBandwidth(counter_2),
         CalcBandwidth(counter_3), count);

  mmio.Write32(channels_enabled | DMC_QOS_CLEAR_CTRL, MEMBW_PORTS_CTRL);
  return ZX_OK;
}

zx_status_t AmlRam::Create(void* context, zx_device_t* parent) {
  zx_status_t status;
  ddk::PDev pdev(parent);
  std::optional<ddk::MmioBuffer> mmio;
  if ((status = pdev.MapMmio(0, &mmio)) != ZX_OK) {
    zxlogf(ERROR, "aml-ram: Failed to map mmio, st = %d\n", status);
    return status;
  }

  fbl::AllocChecker ac;
  auto device = fbl::make_unique_checked<AmlRam>(&ac, parent, *std::move(mmio));
  if (!ac.check()) {
    zxlogf(ERROR, "aml-ram: Failed to allocate device memory\n");
    return ZX_ERR_NO_MEMORY;
  }

  status = device->DdkAdd("ram", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-ram: Failed to add ram device, st = %d\n", status);
    return status;
  }

  __UNUSED auto* dummy = device.release();
  return ZX_OK;
}

AmlRam::AmlRam(zx_device_t* parent, ddk::MmioBuffer mmio)
    : DeviceType(parent), mmio_(std::move(mmio)) {
  if (driver_get_log_flags() & ZX_LOG_TRACE) {
    auto status = zx::event::create(0u, &shutdown_);
    ZX_ASSERT(status == ZX_OK);
    thread_ = std::thread([this] { ReadLoop(); });
  }
}

void AmlRam::DdkSuspendNew(ddk::SuspendTxn txn) {
  Shutdown();
  txn.Reply(ZX_OK, txn.requested_state());
}

void AmlRam::DdkRelease() {
  Shutdown();
  delete this;
}

zx_status_t AmlRam::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  // TODO(cpu): Implement dispatch of the fuchsia.hardware.ram.metrics
  // FIDL interface. This controls which ports to sample rather than
  // hardcoding them in ReadLoop().
  return transaction.Status();
}

// TODO(cpu): Remove this function once the FIDL protocol is implemented.
void AmlRam::ReadLoop() {
  constexpr uint32_t kDecoder = PortID_HEVC_FRONT | PortID_HEVC_BACK | PortID_VDEC | PortID_HCODEC;
  constexpr uint32_t kVPU =
      PortID_VPU_R1 | PortID_VPU_R2 | PortID_VPU_R3 | PortID_VPU_W1 | PortID_VPU_W2;

  // Sample the following 4 channels for the time being.
  const BwPorts kPorts = {{PortID_ARM_AE, PortID_MALI, kDecoder, kVPU}};
  InitBandwithTimer(mmio_);

  for (;;) {
    auto status = ReadBandwithCounters(mmio_, kPorts, shutdown_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-ram: error reading counters, st =%d\n", status);
      return;
    }

    zx_signals_t observed = 0u;
    status = shutdown_.wait_one(ZX_EVENT_SIGNALED, zx::deadline_after(zx::sec(3)), &observed);
    if (status == ZX_ERR_TIMED_OUT) {
      continue;
    } else if (status != ZX_OK) {
      zxlogf(ERROR, "aml-ram: error in wait_one, st =%d\n", status);
      return;
    } else {
      // Normal shutdown.
      zxlogf(TRACE, "aml-ram: loop shutdown\n");
      return;
    }
  };
}

void AmlRam::Shutdown() {
  if (shutdown_) {
    shutdown_.signal(0u, ZX_EVENT_SIGNALED);
    thread_.join();
    shutdown_.reset();
  }
}

}  // namespace amlogic_ram

static constexpr zx_driver_ops_t aml_ram_driver_ops = []() {
  zx_driver_ops_t result = {};
  result.version = DRIVER_OPS_VERSION;
  result.bind = amlogic_ram::AmlRam::Create;

  return result;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_ram, aml_ram_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_RAM_CTL),
    // This driver can likely support S905D3 and T931 in the future.
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
ZIRCON_DRIVER_END(aml_ram)
    // clang-format on
