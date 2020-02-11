// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/zircon-internal/thread_annotations.h>
#include <platform.h>
#include <reg.h>
#include <string.h>
#include <zircon/boot/driver-config.h>
#include <zircon/types.h>

#include <arch/arm64/periphmap.h>
#include <kernel/lockdep.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>
#include <pdev/driver.h>
#include <pdev/watchdog.h>
#include <vm/physmap.h>

class GenericWatchdog32 {
 public:
  constexpr GenericWatchdog32() {}

  // TODO(johngro) : for now, don't actually declare a destructor.  We don't
  // want to end up registering a global .dtor for no reason.  If/when we get to
  // the point where the kernel can actually "exit" for sanitizer analysis, we
  // will want to come back here and enable this.
  //~GenericWatchdog32() { timer_cancel(&pet_timer_}; }

  // Early init takes place while we are still single threaded, and don't need
  // to worry about thread safety.
  void InitEarly(const void* driver_data, uint32_t length);
  void Init(const void*, uint32_t);

  // Actions
  void Pet() TA_EXCL(lock_) {
    Guard<SpinLock, IrqSave> guard{&lock_};
    PetLocked();
  }

  zx_status_t SetEnabled(bool enb) TA_EXCL(lock_) {
    Guard<SpinLock, IrqSave> guard{&lock_};

    // Nothing to do if we are already in the desired state.
    if (enb == is_enabled_) {
      return ZX_OK;
    }

    if (!(enb ? cfg_.enable_action.addr : cfg_.disable_action.addr)) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    is_enabled_ = enb;

    if (is_enabled_) {
      // Enable the timer, then immediately pet the watchdog, and set up the pet
      // timer.
      TakeAction(cfg_.enable_action);
      HandlePetTimer();
    } else {
      // Disable the watchdog and cancel any in-flight timer.
      TakeAction(cfg_.disable_action);
      timer_cancel(&pet_timer_);
    }

    return ZX_OK;
  }

  // Accessors
  zx_duration_t timeout_nsec() const { return cfg_.watchdog_period_nsec; }
  bool is_enabled() const TA_EXCL(lock_) {
    Guard<SpinLock, IrqSave> guard{&lock_};
    return is_enabled_;
  }

  zx_time_t last_pet_time() const TA_EXCL(lock_) {
    Guard<SpinLock, IrqSave> guard{&lock_};
    return last_pet_time_;
  }

 private:
  static bool TranslatePAddr(uint64_t* paddr);
  void TakeAction(const dcfg_generic_32bit_watchdog_action_t& action) TA_REQ(lock_) {
    uint32_t val = readl(action.addr);
    val &= ~action.clr_mask;
    val |= action.set_mask;
    writel(val, action.addr);
  }

  void PetLocked() TA_REQ(lock_) {
    last_pet_time_ = current_time();
    TakeAction(cfg_.pet_action);
  }
  void HandlePetTimer() TA_REQ(lock_);
  void PretendLocked() TA_ASSERT(lock_) {}

  mutable DECLARE_SPINLOCK(GenericWatchdog32) lock_;
  dcfg_generic_32bit_watchdog_t cfg_{};
  zx_status_t early_init_result_ = ZX_ERR_INTERNAL;
  zx_time_t last_pet_time_ TA_GUARDED(lock_) = 0;
  timer_t pet_timer_ TA_GUARDED(lock_) TIMER_INITIAL_VALUE(pet_timer_);
  bool is_enabled_ TA_GUARDED(lock_) = false;
};

static GenericWatchdog32 g_watchdog;

static const pdev_watchdog_ops_t THUNKS = {
    .pet = []() { g_watchdog.Pet(); },
    .set_enabled = [](bool enb) { return g_watchdog.SetEnabled(enb); },
    .is_enabled = []() { return g_watchdog.is_enabled(); },
    .get_timeout_nsec = []() { return g_watchdog.timeout_nsec(); },
    .get_last_pet_time = []() { return g_watchdog.last_pet_time(); },
};

void GenericWatchdog32::InitEarly(const void* driver_data, uint32_t length) {
  // "Assert" that we are holding the lock.  While this is technically a no-op,
  // it tells the thread analyzer to pretend that we are holding the lock.  We
  // are in the early init stage of boot, so it is too early to need to worry
  // about multi-thread safety issues, but we don't want to actually be
  // obtaining and releasing the spin lock at this point.  Along with the
  // annotations in the class, pretending that we are holding the lock at this
  // point in time will make certain that we are following all of the locking
  // rules.
  PretendLocked();

  // Sanity check our config first.  If they are invalid, we cannot proceed
  // (and if the watchdog is already enable, we are gonna end up rebooting).
  //
  // Sadly, it is too early to do any logging.  If we manage to make it to the
  // PLATFORM init level, we will log the errors there.

  // We must have config, and that config must be of the proper length.  If that
  // checks out, go ahead and copy the config.
  if ((driver_data == nullptr) || (length != sizeof(cfg_))) {
    early_init_result_ = ZX_ERR_INVALID_ARGS;
    return;
  }
  memcpy(&cfg_, driver_data, sizeof(cfg_));

  // All generic watchdog drivers must have some way of petting the dog.
  // Enable/disable is optional, but not petting.
  if (!cfg_.pet_action.addr) {
    early_init_result_ = ZX_ERR_INVALID_ARGS;
    return;
  }

  // The watchdog period must be at least 1 mSec.  We don't want to spend 15% of
  // our CPU petting the watchdog all of the time.
  if (cfg_.watchdog_period_nsec < KDRV_GENERIC_32BIT_WATCHDOG_MIN_PERIOD) {
    early_init_result_ = ZX_ERR_INVALID_ARGS;
    return;
  }

  // Great! Things look good.  Translate the physical addresses for the various
  // actions to virtual addresses.  If we cannot translate the pet address, we
  // have a problem.  If we cannot translate the enable or disable address, then
  // so be it.  That functionality will be unavailable, but at least we can pet
  // the dog.
  if (!TranslatePAddr(&cfg_.pet_action.addr)) {
    early_init_result_ = ZX_ERR_IO;
  }

  TranslatePAddr(&cfg_.enable_action.addr);
  TranslatePAddr(&cfg_.disable_action.addr);

  // Record our initial enabled/disabled state.
  is_enabled_ = (cfg_.flags & KDRV_GENERIC_32BIT_WATCHDOG_FLAG_ENABLED) != 0;

  // Register our driver.  Note that the pdev layer is going to hold onto our
  // thunk table, not make a copy.  We need to be sure that we don't let our
  // table go out of scope (which is why it is file local).
  pdev_register_watchdog(&THUNKS);

  // Finally, if we are currently enabled, be sure to pet the dog ASAP.  We
  // don't want it to fire while we are bringing up the kernel to the point
  // where we can do stuff like set timers.
  PetLocked();

  // Things went well.  Make sure the later init stage knows that.
  early_init_result_ = ZX_OK;
}

void GenericWatchdog32::Init(const void*, uint32_t) {
  Guard<SpinLock, IrqSave> guard{&lock_};

  // Ok, we are much farther along in the boot now.  We should be able to do
  // things like report errors and set timers at this point.  Start by checking
  // out how things went during early init.  If things went poorly, try to log
  // why.  Hopefully the watchdog is currently disabled, or we are going to
  // reboot Real Soon Now(tm).
  if (early_init_result_ != ZX_OK) {
    dprintf(
        INFO,
        "WDT: Generic watchdog driver attempted to load, but failed during early init (res %d).\n",
        early_init_result_);
    return;
  }

  // Report that the driver has successfully loaded, along with some handy info
  // about the hardware state.
  dprintf(INFO, "WDT: Generic watchdog driver loaded.  Period (%ld.%03ld mSec) Enabled (%s)\n",
          timeout_nsec() / ZX_MSEC(1), (timeout_nsec() % ZX_MSEC(1)) / ZX_USEC(1),
          is_enabled_ ? "yes" : "no");

  // If we are enabled, pet the dog now and set our pet timer.
  HandlePetTimer();
}

void GenericWatchdog32::HandlePetTimer() {
  if (is_enabled_) {
    PetLocked();
    Deadline next_pet_time{last_pet_time_ + (timeout_nsec() / 2),
                           {(timeout_nsec() / 4), TIMER_SLACK_EARLY}};
    timer_set(
        &pet_timer_, next_pet_time,
        [](struct timer*, zx_time_t now, void* arg) {
          auto thiz = reinterpret_cast<GenericWatchdog32*>(arg);
          Guard<SpinLock, IrqSave> guard{&thiz->lock_};
          thiz->HandlePetTimer();
        },
        this);
  }
}

bool GenericWatchdog32::TranslatePAddr(uint64_t* paddr) {
  // Translate a register's physical address to a virtual address so we can read
  // and write it.  If we cannot, for some reason, return an error.  If the
  // address is already nullptr, just leave it that way.  This is not an error,
  // it just means that the register for this action is not available.
  if (*paddr == 0) {
    return true;
  }

  *paddr = periph_paddr_to_vaddr(static_cast<paddr_t>(*paddr));

  return (*paddr != 0);
}

LK_PDEV_INIT(
    generic_32bit_watchdog_init_early, KDRV_GENERIC_32BIT_WATCHDOG,
    [](const void* driver_data, uint32_t length) { g_watchdog.InitEarly(driver_data, length); },
    LK_INIT_LEVEL_PLATFORM_EARLY)
LK_PDEV_INIT(
    generic_32bit_watchdog_init, KDRV_GENERIC_32BIT_WATCHDOG,
    [](const void* driver_data, uint32_t length) { g_watchdog.Init(driver_data, length); },
    LK_INIT_LEVEL_PLATFORM)
