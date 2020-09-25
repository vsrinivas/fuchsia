// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/intrin.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <arch/x86/feature.h>
#include <dev/hw_rng.h>
#include <fbl/algorithm.h>
#include <ktl/algorithm.h>
#include <lk/init.h>

enum entropy_instr {
  ENTROPY_INSTR_RDSEED,
  ENTROPY_INSTR_RDRAND,
};
static ssize_t get_entropy_from_instruction(void* buf, size_t len, enum entropy_instr instr);
static ssize_t get_entropy_from_rdseed(void* buf, size_t len);
static ssize_t get_entropy_from_rdrand(void* buf, size_t len);

/* @brief Get entropy from the CPU using RDSEED.
 *
 * len must be at most SSIZE_MAX
 *
 * It will retry the RDSEED instruction until |len| bytes are written to |buf|.
 *
 * Returns the number of bytes written to the buffer on success (potentially 0),
 * and a negative value on error.
 */
static ssize_t get_entropy_from_cpu(void* buf, size_t len) {
  /* TODO(security, fxbug.dev/30930): Move this to a shared kernel/user lib, so we can write usermode
   * tests against this code */

  if (len >= SSIZE_MAX) {
    static_assert(ZX_ERR_INVALID_ARGS < 0, "");
    return ZX_ERR_INVALID_ARGS;
  }

  if (x86_feature_test(X86_FEATURE_RDSEED)) {
    return get_entropy_from_rdseed(buf, len);
  } else if (x86_feature_test(X86_FEATURE_RDRAND)) {
    return get_entropy_from_rdrand(buf, len);
  }

  /* We don't have an entropy source */
  static_assert(ZX_ERR_NOT_SUPPORTED < 0, "");
  return ZX_ERR_NOT_SUPPORTED;
}

__attribute__((target("rdrnd,rdseed"))) static bool instruction_step(enum entropy_instr instr,
                                                                     unsigned long long int* val) {
  switch (instr) {
    case ENTROPY_INSTR_RDRAND:
      return _rdrand64_step(val);
    case ENTROPY_INSTR_RDSEED:
      return _rdseed64_step(val);
    default:
      panic("Invalid entropy instruction %d\n", (int)instr);
  }
}

static ssize_t get_entropy_from_instruction(void* buf, size_t len, enum entropy_instr instr) {
  size_t written = 0;
  while (written < len) {
    unsigned long long int val = 0;
    if (!instruction_step(instr, &val)) {
      continue;
    }
    const size_t to_copy = ktl::min(len - written, sizeof(val));
    memcpy(static_cast<uint8_t*>(buf) + written, &val, to_copy);
    written += to_copy;
  }
  DEBUG_ASSERT(written == len);

  return (ssize_t)written;
}

static ssize_t get_entropy_from_rdseed(void* buf, size_t len) {
  return get_entropy_from_instruction(buf, len, ENTROPY_INSTR_RDSEED);
}

static ssize_t get_entropy_from_rdrand(void* buf, size_t len) {
  // TODO(security, fxbug.dev/30929): This method is not compliant with Intel's "Digital Random
  // Number Generator (DRNG) Software Implementation Guide".  We are using
  // rdrand in a way that is explicitly against their recommendations.  This
  // needs to be corrected, but this fallback is a compromise to allow our
  // development platforms that don't support RDSEED to get some degree of
  // hardware-based randomization.
  return get_entropy_from_instruction(buf, len, ENTROPY_INSTR_RDRAND);
}

static size_t intel_hw_rng_get_entropy(void* buf, size_t len) {
  if (!len) {
    return 0;
  }

  ssize_t res = get_entropy_from_cpu(buf, len);
  if (res < 0) {
    return 0;
  }
  return (size_t)res;
}

static struct hw_rng_ops ops = {
    .hw_rng_get_entropy = intel_hw_rng_get_entropy,
};

static void intel_rng_init(uint level) { hw_rng_register(&ops); }

LK_INIT_HOOK(intel_rng_init, intel_rng_init, LK_INIT_LEVEL_PLATFORM_EARLY + 1)
