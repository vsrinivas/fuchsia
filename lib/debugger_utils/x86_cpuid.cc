// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file is based on zircon/kernel/arch/x86/feature.c.
// TODO(dje): As with generic elf, dwarf, et.al, move to application
// independent library.

#include "x86_cpuid.h"

#include <cpuid.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace debugserver {

// Trick to get a 1 of the right size.
#define ONE(x) (1 + ((x) - (x)))
#define BITS_SHIFT(x, high, low) \
  (((x) >> (low)) & ((ONE(x) << ((high) - (low) + 1)) - 1))

// Note: cpuid state is constant once computed, and thus isn't guarded.

static struct x86_cpuid_leaf cpuid[MAX_SUPPORTED_CPUID + 1];
static struct x86_cpuid_leaf
    cpuid_ext[MAX_SUPPORTED_CPUID_EXT - X86_CPUID_EXT_BASE + 1];
static uint32_t max_cpuid = 0;
static uint32_t max_ext_cpuid = 0;

static enum x86_vendor_list x86_vendor;

static struct x86_model_info model_info;

static std::mutex cpuid_mutex;

static bool initialized = false;  // TODO(dje): add guard annotation

static const x86_cpuid_leaf* x86_get_cpuid_leaf_raw(enum x86_cpuid_leaf_num leaf);

void x86_feature_init(void) {
  std::lock_guard<std::mutex> lock(cpuid_mutex);

  if (initialized) return;

  // test for cpuid count
  __cpuid(0, cpuid[0].a, cpuid[0].b, cpuid[0].c, cpuid[0].d);

  max_cpuid = cpuid[0].a;
  if (max_cpuid > MAX_SUPPORTED_CPUID) max_cpuid = MAX_SUPPORTED_CPUID;

  // figure out the vendor
  union {
    uint32_t vendor_id[3];
    char vendor_string[13];
  } vu;
  vu.vendor_id[0] = cpuid[0].b;
  vu.vendor_id[1] = cpuid[0].d;
  vu.vendor_id[2] = cpuid[0].c;
  vu.vendor_string[12] = '\0';
  if (!strcmp(vu.vendor_string, "GenuineIntel")) {
    x86_vendor = X86_VENDOR_INTEL;
  } else if (!strcmp(vu.vendor_string, "AuthenticAMD")) {
    x86_vendor = X86_VENDOR_AMD;
  } else {
    x86_vendor = X86_VENDOR_UNKNOWN;
  }

  // read in the base cpuids
  for (uint32_t i = 1; i <= max_cpuid; i++) {
    __cpuid_count(i, 0, cpuid[i].a, cpuid[i].b, cpuid[i].c, cpuid[i].d);
  }

  // test for extended cpuid count
  __cpuid(X86_CPUID_EXT_BASE, cpuid_ext[0].a, cpuid_ext[0].b, cpuid_ext[0].c,
          cpuid_ext[0].d);

  max_ext_cpuid = cpuid_ext[0].a;
  if (max_ext_cpuid > MAX_SUPPORTED_CPUID_EXT)
    max_ext_cpuid = MAX_SUPPORTED_CPUID_EXT;

  // read in the extended cpuids
  for (uint32_t i = X86_CPUID_EXT_BASE + 1; i - 1 < max_ext_cpuid; i++) {
    uint32_t index = i - X86_CPUID_EXT_BASE;
    __cpuid_count(i, 0, cpuid_ext[index].a, cpuid_ext[index].b,
                  cpuid_ext[index].c, cpuid_ext[index].d);
  }

  // populate the model info
  const struct x86_cpuid_leaf* leaf =
      x86_get_cpuid_leaf_raw(X86_CPUID_MODEL_FEATURES);
  if (leaf) {
    model_info.processor_type = BITS_SHIFT(leaf->a, 13, 12);
    model_info.family = BITS_SHIFT(leaf->a, 11, 8);
    model_info.model = BITS_SHIFT(leaf->a, 7, 4);
    model_info.stepping = BITS_SHIFT(leaf->a, 3, 0);
    model_info.display_family = model_info.family;
    model_info.display_model = model_info.model;

    if (model_info.family == 0xf) {
      model_info.display_family += BITS_SHIFT(leaf->a, 27, 20);
    }
    if (model_info.family == 0xf || model_info.family == 0x6) {
      model_info.display_model += BITS_SHIFT(leaf->a, 19, 16) << 4;
    }
  }

  initialized = true;
}

bool x86_get_cpuid_subleaf(enum x86_cpuid_leaf_num num, uint32_t subleaf,
                           struct x86_cpuid_leaf* leaf) {
  x86_feature_init();

  if (num < X86_CPUID_EXT_BASE) {
    if (num > max_cpuid) return false;
  } else if (num > max_ext_cpuid) {
    return false;
  }

  __cpuid_count((uint32_t)num, subleaf, leaf->a, leaf->b, leaf->c, leaf->d);
  return true;
}

static const x86_cpuid_leaf* x86_get_cpuid_leaf_raw(enum x86_cpuid_leaf_num leaf) {
  if (leaf < X86_CPUID_EXT_BASE) {
    if (leaf > max_cpuid) return nullptr;

    return &cpuid[leaf];
  } else {
    if (leaf > max_ext_cpuid) return nullptr;

    return &cpuid_ext[(uint32_t)leaf - (uint32_t)X86_CPUID_EXT_BASE];
  }
}

const x86_cpuid_leaf* x86_get_cpuid_leaf(enum x86_cpuid_leaf_num leaf) {
  x86_feature_init();
  return x86_get_cpuid_leaf_raw(leaf);
}

bool x86_feature_test(struct x86_cpuid_bit bit) {
  FXL_DCHECK(bit.word <= 3 && bit.bit <= 31);

  if (bit.word > 3 || bit.bit > 31) return false;

  const x86_cpuid_leaf* leaf = x86_get_cpuid_leaf(bit.leaf_num);
  if (!leaf) return false;

  switch (bit.word) {
    case 0:
      return !!((1u << bit.bit) & leaf->a);
    case 1:
      return !!((1u << bit.bit) & leaf->b);
    case 2:
      return !!((1u << bit.bit) & leaf->c);
    case 3:
      return !!((1u << bit.bit) & leaf->d);
    default:
      return false;
  }
}

bool x86_topology_enumerate(uint8_t level, struct x86_topology_level* info) {
  uint32_t eax, ebx, ecx, edx;
  __cpuid_count(X86_CPUID_TOPOLOGY, level, eax, ebx, ecx, edx);

  uint8_t type = (ecx >> 8) & 0xff;
  if (type == X86_TOPOLOGY_INVALID) return false;

  info->right_shift = eax & 0x1f;
  info->type = type;
  return true;
}

const struct x86_model_info* x86_get_model(void) {
  x86_feature_init();
  return &model_info;
}

// N.B. The output is parsed by spt-sideband.cc.

void x86_feature_debug(FILE* out) {
  static const struct {
    x86_cpuid_bit bit;
    const char* name;
  } features[] = {
      {X86_FEATURE_FPU, "fpu"},
      {X86_FEATURE_SSE, "sse"},
      {X86_FEATURE_SSE2, "sse2"},
      {X86_FEATURE_SSE3, "sse3"},
      {X86_FEATURE_SSSE3, "ssse3"},
      {X86_FEATURE_SSE4_1, "sse4.1"},
      {X86_FEATURE_SSE4_2, "sse4.2"},
      {X86_FEATURE_MMX, "mmx"},
      {X86_FEATURE_AVX, "avx"},
      {X86_FEATURE_AVX2, "avx2"},
      {X86_FEATURE_FXSR, "fxsr"},
      {X86_FEATURE_XSAVE, "xsave"},
      {X86_FEATURE_AESNI, "aesni"},
      {X86_FEATURE_TSC_ADJUST, "tsc_adj"},
      {X86_FEATURE_SMEP, "smep"},
      {X86_FEATURE_SMAP, "smap"},
      {X86_FEATURE_RDRAND, "rdrand"},
      {X86_FEATURE_RDSEED, "rdseed"},
      {X86_FEATURE_PT, "pt"},
      {X86_FEATURE_PKU, "pku"},
      {X86_FEATURE_SYSCALL, "syscall"},
      {X86_FEATURE_NX, "nx"},
      {X86_FEATURE_HUGE_PAGE, "huge"},
      {X86_FEATURE_RDTSCP, "rdtscp"},
      {X86_FEATURE_INVAR_TSC, "invar_tsc"},
      {X86_FEATURE_TSC_DEADLINE, "tsc_deadline"},
  };

  x86_feature_init();

  const char* vendor_string;
  switch (x86_vendor) {
    default:
    case X86_VENDOR_UNKNOWN:
      vendor_string = "unknown";
      break;
    case X86_VENDOR_INTEL:
      vendor_string = "Intel";
      break;
    case X86_VENDOR_AMD:
      vendor_string = "AMD";
      break;
  }
  fprintf(out, "Vendor: %s\n", vendor_string);

  auto model = x86_get_model();
  fprintf(out, "Model:\n");
  fprintf(out, "  processor: %u\n", model->processor_type);
  // TODO(dje): It's not clear which "variant" of family/model IPT wants,
  // but it seems like it would want the more detailed version, so that's
  // what we use.
  fprintf(out, "  base family: %u\n", model->family);
  fprintf(out, "  base model: %u\n", model->model);
  fprintf(out, "  family: %u\n", model->display_family);
  fprintf(out, "  model: %u\n", model->display_model);
  fprintf(out, "  stepping: %u\n", model->stepping);

  fprintf(out, "Features:\n");
  size_t col = 0;
  for (auto& f : features) {
    if (x86_feature_test(f.bit)) col += fprintf(out, " %s", f.name);
    if (col >= 80) {
      fprintf(out, "\n");
      col = 0;
    }
  }
  if (col > 0) fprintf(out, "\n");
}

}  // namespace debugserver
