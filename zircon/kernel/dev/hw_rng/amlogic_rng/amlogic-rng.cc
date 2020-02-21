// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <reg.h>
#include <zircon/boot/driver-config.h>

#include <arch/arm64/periphmap.h>
#include <dev/hw_rng.h>
#include <explicit-memory/bytes.h>
#include <fbl/algorithm.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <pdev/driver.h>

// Mask for the bit indicating RNG status.
#define AML_RNG_READY 1

// Register for RNG data
static vaddr_t rng_data = 0;
// Register whose 1st bit indicates RNG status: 1->ready, 0->not ready.
static vaddr_t rng_status = 0;
// Hardware RNG refresh time in microsecond.
static uint64_t rng_refresh_interval_usec = 0;

// Size of each RNG draw.
constexpr size_t kRngDrawSize = 4;
// Max number of retry
constexpr size_t kMaxRetry = 10000;

static size_t amlogic_hw_rng_get_entropy(void* buf, size_t len) {
  if (buf == nullptr) {
    return 0;
  }

  char* dest = static_cast<char*>(buf);
  size_t total_read = 0;
  size_t retry = 0;

  while (len > 0) {
    // Retry until RNG is ready.
    while ((readl(rng_status) & AML_RNG_READY) != 1) {
      if (retry > kMaxRetry) {
        mandatory_memset(&buf, 0, len);
        return 0;
      }

      Thread::Current::SleepRelative(ZX_USEC(1));
      retry++;
    }

    uint32_t read_buf = readl(rng_data);
    static_assert(sizeof(read_buf) == kRngDrawSize);

    size_t read_size = fbl::min(len, kRngDrawSize);
    memcpy(dest + total_read, &read_buf, read_size);
    mandatory_memset(&read_buf, 0, sizeof(read_buf));

    total_read += read_size;
    len -= read_size;

    // Hardware RNG expected to be ready after an interval.
    Thread::Current::SleepRelative(ZX_USEC(rng_refresh_interval_usec));
  }
  return total_read;
}

static struct hw_rng_ops ops = {
    .hw_rng_get_entropy = amlogic_hw_rng_get_entropy,
};

static void amlogic_rng_init(const void* driver_data, uint32_t length) {
  ASSERT(length >= sizeof(dcfg_amlogic_rng_driver_t));
  auto driver = static_cast<const dcfg_amlogic_rng_driver_t*>(driver_data);
  ASSERT(driver->rng_data_phys && driver->rng_status_phys);

  rng_data = periph_paddr_to_vaddr(driver->rng_data_phys);
  rng_status = periph_paddr_to_vaddr(driver->rng_status_phys);
  rng_refresh_interval_usec = driver->rng_refresh_interval_usec;

  ASSERT(rng_data);
  ASSERT(rng_status);
  ASSERT(rng_refresh_interval_usec > 0);

  hw_rng_register(&ops);
}

LK_PDEV_INIT(amlogic_rng_init, KDRV_AMLOGIC_RNG, amlogic_rng_init, LK_INIT_LEVEL_PLATFORM)
