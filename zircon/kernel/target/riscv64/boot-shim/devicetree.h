// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_TARGET_RISCV64_BOOT_SHIM_DEVICETREE_H_
#define ZIRCON_KERNEL_TARGET_RISCV64_BOOT_SHIM_DEVICETREE_H_

#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS;

typedef struct dt_slice {
  uint8_t *data;
  uint32_t size;
} dt_slice_t;

struct devicetree_header {
  uint32_t magic;
  uint32_t size;
  uint32_t off_struct;   // offset from start to DT 'structure'
  uint32_t off_strings;  // offset from start to stringdata
  uint32_t off_reserve;  // offset from start to reserve memory map
  uint32_t version;
  uint32_t version_compat;  // last compatible version
  uint32_t boot_cpuid;
  uint32_t sz_strings;  // size of stringdata
  uint32_t sz_struct;   // size of DT 'structure'
};

typedef struct devicetree {
  dt_slice_t top;
  dt_slice_t dt;
  dt_slice_t ds;
  struct devicetree_header hdr;
  void (*error)(const char *msg);
} devicetree_t;

typedef int (*dt_node_cb)(int depth, const char *name, void *cookie);
typedef int (*dt_prop_cb)(const char *name, uint8_t *data, uint32_t size, void *cookie);

int dt_init(devicetree_t *dt, void *data, uint32_t len);
int dt_walk(devicetree_t *dt, dt_node_cb ncb, dt_prop_cb pcb, void *cookie);

uint32_t dt_rd32(uint8_t *data);
void dt_wr32(uint32_t n, uint8_t *data);

__END_CDECLS;

#endif  // ZIRCON_KERNEL_TARGET_RISCV64_BOOT_SHIM_DEVICETREE_H_
