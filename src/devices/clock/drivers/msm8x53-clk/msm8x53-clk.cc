// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msm8x53-clk.h"

#include <fuchsia/hardware/clock/c/fidl.h>
#include <lib/device-protocol/pdev.h>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/clockimpl.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/protocol/platform/bus.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <hwreg/bitfields.h>

#include "msm8x53-clk-regs.h"

namespace clk {

namespace {

constexpr char kMsmClkName[] = "msm-clk";
constexpr uint32_t kRcgUpdateTimeoutUsec = 500;
constexpr uint64_t kRcgRateUnset = 0;
constexpr uint32_t kCfgRcgrDivMask = (0x1f << 0);
constexpr uint32_t kCfgRcgrSrcSelMask = (0x7 << 8);

}  // namespace

zx_status_t Msm8x53Clk::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "msm-clk: failed to get pdev protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> mmio;
  status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "msm-clk: failed to map cc_base mmio, st = %d", status);
    return status;
  }

  std::unique_ptr<Msm8x53Clk> device(new Msm8x53Clk(parent, *std::move(mmio)));

  status = device->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "msm-clk: failed to initialize, st = %d", status);
    return status;
  }

  status = device->DdkAdd(kMsmClkName);
  if (status != ZX_OK) {
    zxlogf(ERROR, "msm-clk: DdkAdd failed, st = %d", status);
    return status;
  }

  // Intentially leak, devmgr owns the memory now.
  __UNUSED auto* unused = device.release();

  return ZX_OK;
}

zx_status_t Msm8x53Clk::Init() {
  fbl::AutoLock lock(&rcg_rates_lock_);
  for (size_t i = 0; i < msm8x53::kRcgClkCount; i++) {
    rcg_rates_[i] = kRcgRateUnset;
  }

  return ZX_OK;
}

zx_status_t Msm8x53Clk::ClockImplEnable(uint32_t index) {
  // Extract the index and the type of the clock from the argument.
  const uint32_t clock_id = msm8x53::MsmClkIndex(index);
  const msm8x53::msm_clk_type clock_type = msm8x53::MsmClkType(index);

  switch (clock_type) {
    case msm8x53::msm_clk_type::kGate:
      return GateClockEnable(clock_id);
    case msm8x53::msm_clk_type::kBranch:
      return BranchClockEnable(clock_id);
    case msm8x53::msm_clk_type::kVoter:
      return VoterClockEnable(clock_id);
    case msm8x53::msm_clk_type::kRcg:
      return RcgClockEnable(clock_id);
  }

  // Unimplemented clock type?
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::ClockImplDisable(uint32_t index) {
  // Extract the index and the type of the clock from the argument.
  const uint32_t clock_id = msm8x53::MsmClkIndex(index);
  const msm8x53::msm_clk_type clock_type = msm8x53::MsmClkType(index);

  switch (clock_type) {
    case msm8x53::msm_clk_type::kGate:
      return GateClockDisable(clock_id);
    case msm8x53::msm_clk_type::kBranch:
      return BranchClockDisable(clock_id);
    case msm8x53::msm_clk_type::kVoter:
      return VoterClockDisable(clock_id);
    case msm8x53::msm_clk_type::kRcg:
      return RcgClockDisable(clock_id);
  }

  // Unimplemented clock type?
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::ClockImplIsEnabled(uint32_t id, bool* out_enabled) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::ClockImplSetRate(uint32_t id, uint64_t hz) {
  const uint32_t index = msm8x53::MsmClkIndex(id);
  const msm8x53::msm_clk_type clock_type = msm8x53::MsmClkType(id);

  switch (clock_type) {
    case msm8x53::msm_clk_type::kRcg: {
      fbl::AutoLock rcg_rates_lock(&rcg_rates_lock_);
      return RcgClockSetRate(index, hz);
    }
    default:
      zxlogf(WARNING, "msm_clk: unsupported clock type: %u", (uint16_t)clock_type);
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::ClockImplSetInput(uint32_t id, uint32_t idx) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::ClockImplGetNumInputs(uint32_t id, uint32_t* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::ClockImplGetInput(uint32_t id, uint32_t* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::ClockImplQuerySupportedRate(uint32_t id, uint64_t max_rate,
                                                    uint64_t* out_best_rate) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::ClockImplGetRate(uint32_t id, uint64_t* out_current_rate) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::AwaitBranchClock(Toggle status, const uint32_t cbcr_reg) {
  // In case the status check register and the clock control register cross
  // a boundary.
  hw_mb();

  // clang-format off
    constexpr uint32_t kReadyMask             = 0xf0000000;
    constexpr uint32_t kBranchEnableVal       = 0x0;
    constexpr uint32_t kBranchDisableVal      = 0x80000000;
    constexpr uint32_t kBranchNocFsmEnableVal = 0x20000000;
  // clang-format on

  constexpr uint32_t kMaxAttempts = 500;
  for (uint32_t attempts = 0; attempts < kMaxAttempts; attempts++) {
    const uint32_t val = mmio_.Read32(cbcr_reg) & kReadyMask;

    switch (status) {
      case Toggle::Enabled:
        if ((val == kBranchEnableVal) || (val == kBranchNocFsmEnableVal)) {
          return ZX_OK;
        }
        break;
      case Toggle::Disabled:
        if (val == kBranchDisableVal) {
          return ZX_OK;
        }
        break;
    }

    zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
  }

  return ZX_ERR_TIMED_OUT;
}

zx_status_t Msm8x53Clk::VoterClockEnable(uint32_t index) {
  if (unlikely(index >= countof(kMsmClkVoters))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const struct clk::msm_clk_voter& clk = kMsmClkVoters[index];

  lock_.Acquire();
  mmio_.SetBits32(clk.bit, clk.vote_reg);
  lock_.Release();

  return AwaitBranchClock(Toggle::Enabled, clk.cbcr_reg);
}

zx_status_t Msm8x53Clk::VoterClockDisable(uint32_t index) {
  if (unlikely(index >= countof(kMsmClkVoters))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const struct clk::msm_clk_voter& clk = kMsmClkVoters[index];

  lock_.Acquire();
  mmio_.ClearBits32(clk.bit, clk.vote_reg);
  lock_.Release();

  return ZX_OK;
}

zx_status_t Msm8x53Clk::BranchClockEnable(uint32_t index) {
  if (unlikely(index >= countof(kMsmClkBranches))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const struct clk::msm_clk_branch& clk = kMsmClkBranches[index];

  lock_.Acquire();
  mmio_.SetBits32(kBranchEnable, clk.reg);
  lock_.Release();

  return AwaitBranchClock(Toggle::Enabled, clk.reg);
}

zx_status_t Msm8x53Clk::BranchClockDisable(uint32_t index) {
  if (unlikely(index >= countof(kMsmClkBranches))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const struct msm_clk_branch& clk = kMsmClkBranches[index];

  lock_.Acquire();
  mmio_.ClearBits32(kBranchEnable, clk.reg);
  lock_.Release();

  return AwaitBranchClock(Toggle::Disabled, clk.reg);
}

zx_status_t Msm8x53Clk::GateClockEnable(uint32_t index) {
  if (unlikely(index >= countof(kMsmClkGates))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const msm_clk_gate_t& clk = kMsmClkGates[index];

  lock_.Acquire();
  mmio_.SetBits32((1u << clk.bit), clk.reg);
  lock_.Release();

  if (clk.delay_us) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(clk.delay_us)));
  }

  return ZX_OK;
}
zx_status_t Msm8x53Clk::GateClockDisable(uint32_t index) {
  if (unlikely(index > countof(kMsmClkGates))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const msm_clk_gate_t& clk = kMsmClkGates[index];

  lock_.Acquire();
  mmio_.ClearBits32(clk.bit, clk.reg);
  lock_.Release();

  if (clk.delay_us) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(clk.delay_us)));
  }

  return ZX_OK;
}

zx_status_t Msm8x53Clk::RcgClockEnable(uint32_t index) {
  if (unlikely(index > countof(kMsmClkRcgs))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const MsmClkRcg& clk = kMsmClkRcgs[index];

  // Check to see if frequency has been set.
  fbl::AutoLock lock(&rcg_rates_lock_);
  if (rcg_rates_[index] == kRcgRateUnset) {
    zxlogf(ERROR, "Attempted to enable RCG %u before setting rate", index);
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t st;

  st = ToggleRcgForceEnable(clk.CmdReg(), Toggle::Enabled);
  if (st != ZX_OK) {
    return st;
  }

  st = RcgClockSetRate(index, rcg_rates_[index]);
  if (st != ZX_OK) {
    return st;
  }

  st = ToggleRcgForceEnable(clk.CmdReg(), Toggle::Disabled);
  if (st != ZX_OK) {
    return st;
  }

  return st;
}

zx_status_t Msm8x53Clk::RcgClockDisable(uint32_t index) {
  // This is a NOP for all clocks that we support.
  // It only needs to be implemented for clocks with non-local children.
  return ZX_OK;
}

zx_status_t Msm8x53Clk::RcgClockSetRate(uint32_t index, uint64_t rate) {
  if (unlikely(index >= countof(kMsmClkRcgs))) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const MsmClkRcg& clk = kMsmClkRcgs[index];

  // Clocks with non-local children or nonlocal control timeouts are
  // currently unimplemented.
  // Clocks with source frequencies that are not fixed are also currently
  // unimplemented.
  if (clk.Unsupported()) {
    zxlogf(ERROR,
           "Attempted to set rate for clock %u which is currently "
           "unimplemented\n",
           index);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Search for the requested frequency in the clock's frequency table.
  const RcgFrequencyTable* table = nullptr;
  for (size_t i = 0; i < clk.TableCount(); i++) {
    if (rate == clk.Table()[i].rate()) {
      table = &clk.Table()[i];
      break;
    }
  }

  if (table == nullptr) {
    // This clock frequency is not supported.
    zxlogf(WARNING, "unsupported clock frequency, clk = %u, rate = %lu", index, rate);
    return ZX_ERR_NOT_SUPPORTED;
  }

  {  // Nested scope for scoped locking
    fbl::AutoLock lock(&lock_);

    switch (clk.Type()) {
      case RcgDividerType::HalfInteger:
        RcgSetRateHalfInteger(clk, table);
        break;
      case RcgDividerType::Mnd:
        RcgSetRateMnd(clk, table);
        break;
    }
  }

  // Update the frequency that we have listed in the RCG table.
  rcg_rates_[index] = rate;

  return ZX_OK;
}

zx_status_t Msm8x53Clk::LatchRcgConfig(const MsmClkRcg& clk) {
  // Whack the config update bit and wait for it to stabilize.
  constexpr uint32_t kCmdRcgrConfigUpdateBit = (0x1 << 0);
  mmio_.SetBits32(kCmdRcgrConfigUpdateBit, clk.CmdReg());

  constexpr uint32_t kMaxAttempts = 500;
  for (uint32_t i = 0; i < kMaxAttempts; i++) {
    const uint32_t cmd_reg = mmio_.Read32(clk.CmdReg());

    if ((cmd_reg & kCmdRcgrConfigUpdateBit) == 0) {
      return ZX_OK;
    }

    zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
  }

  zxlogf(WARNING, "Failed to latch RCG config");
  return ZX_ERR_TIMED_OUT;
}

zx_status_t Msm8x53Clk::RcgSetRateHalfInteger(const MsmClkRcg& clk,
                                              const RcgFrequencyTable* table) {
  uint32_t val;

  val = mmio_.Read32(clk.CfgReg());
  val &= ~(kCfgRcgrDivMask | kCfgRcgrSrcSelMask);
  val |= table->predev_parent();
  mmio_.Write32(val, clk.CfgReg());

  return LatchRcgConfig(clk);
}

zx_status_t Msm8x53Clk::RcgSetRateMnd(const MsmClkRcg& clk, const RcgFrequencyTable* table) {
  uint32_t cfg = mmio_.Read32(clk.CfgReg());

  constexpr uint32_t kMndModeMask = (0x3 << 12);
  constexpr uint32_t kMndDualEdgeMode = (0x2 << 12);

  mmio_.Write32(table->m(), clk.MReg());
  mmio_.Write32(table->n(), clk.NReg());
  mmio_.Write32(table->d(), clk.DReg());

  cfg = mmio_.Read32(clk.CfgReg());
  cfg &= ~(kCfgRcgrDivMask | kCfgRcgrSrcSelMask);
  cfg |= table->predev_parent();

  cfg &= ~kMndModeMask;
  if (table->n() != 0) {
    cfg |= kMndDualEdgeMode;
  }
  mmio_.Write32(cfg, clk.CfgReg());

  return LatchRcgConfig(clk);
}

zx_status_t Msm8x53Clk::ToggleRcgForceEnable(uint32_t rcgr_cmd_offset, Toggle toggle) {
  constexpr uint32_t kRcgForceDisableDelayUSeconds = 100;
  constexpr uint32_t kRcgRootEnableBit = (1 << 1);
  zx_status_t result = ZX_OK;

  switch (toggle) {
    case Toggle::Enabled:
      lock_.Acquire();
      mmio_.SetBits32(kRcgRootEnableBit, rcgr_cmd_offset);
      result = AwaitRcgEnableLocked(rcgr_cmd_offset);
      lock_.Release();
      break;
    case Toggle::Disabled:
      lock_.Acquire();
      mmio_.ClearBits32(kRcgRootEnableBit, rcgr_cmd_offset);
      lock_.Release();
      zx_nanosleep(zx_deadline_after(ZX_USEC(kRcgForceDisableDelayUSeconds)));
      break;
  }
  return result;
}

zx_status_t Msm8x53Clk::AwaitRcgEnableLocked(uint32_t rcgr_cmd_offset) {
  for (uint32_t i = 0; i < kRcgUpdateTimeoutUsec; i++) {
    auto rcg_ctrl = RcgClkCmd::Read(rcgr_cmd_offset).ReadFrom(&mmio_);

    if (rcg_ctrl.root_status() == 0) {
      return ZX_OK;
    }

    zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
  }

  return ZX_ERR_TIMED_OUT;
}

zx_status_t Msm8x53Clk::Bind() { return ZX_OK; }
void Msm8x53Clk::DdkUnbindNew(ddk::UnbindTxn txn) {
  fbl::AutoLock lock(&lock_);

  mmio_.reset();

  txn.Reply();
}

void Msm8x53Clk::DdkRelease() { delete this; }

}  // namespace clk

static constexpr zx_driver_ops_t msm8x53_clk_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = clk::Msm8x53Clk::Create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(msm8x53_clk, msm8x53_clk_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_QUALCOMM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_QUALCOMM_CLOCK),
ZIRCON_DRIVER_END(msm8x53_clk)
