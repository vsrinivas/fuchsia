// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.platform.bus banjo file

#ifndef SRC_DEVICES_BUS_LIB_FUCHSIA_HARDWARE_PLATFORM_BUS_INCLUDE_FUCHSIA_HARDWARE_PLATFORM_BUS_C_BANJO_H_
#define SRC_DEVICES_BUS_LIB_FUCHSIA_HARDWARE_PLATFORM_BUS_INCLUDE_FUCHSIA_HARDWARE_PLATFORM_BUS_C_BANJO_H_

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef struct pbus_sys_suspend pbus_sys_suspend_t;
typedef struct pbus_smc pbus_smc_t;
typedef struct pbus_mmio pbus_mmio_t;
typedef struct pbus_metadata pbus_metadata_t;
typedef struct pbus_irq pbus_irq_t;
typedef struct pbus_bti pbus_bti_t;
typedef struct pbus_bootloader_info pbus_bootloader_info_t;
typedef struct pbus_boot_metadata pbus_boot_metadata_t;
typedef struct pbus_dev pbus_dev_t;
typedef struct pbus_board_info pbus_board_info_t;
typedef struct pbus_protocol pbus_protocol_t;
typedef struct pbus_protocol_ops pbus_protocol_ops_t;

// Declarations
struct pbus_sys_suspend {
  zx_status_t (*callback)(void* ctx, uint8_t requested_state, bool enable_wake,
                          uint8_t suspend_reason, uint8_t* out_out_state);
  void* ctx;
};

struct pbus_smc {
  // The device is granted the ability to make SMC calls with service call numbers ranging from
  // service_call_num_base to service_call_num_base + count - 1.
  uint32_t service_call_num_base;
  uint32_t count;
  // The device has exclusive access to this smc range.
  bool exclusive;
};

struct pbus_mmio {
  // Physical address of MMIO region.
  // Does not need to be page aligned.
  zx_paddr_t base;
  // Length of MMIO region in bytes.
  // Does not need to be page aligned.
  uint64_t length;
};

// Device metadata.
struct pbus_metadata {
  // Metadata type.
  uint32_t type;
  // Pointer to metadata.
  const uint8_t* data_buffer;
  size_t data_size;
};

struct pbus_irq {
  uint32_t irq;
  // `ZX_INTERRUPT_MODE_*` flags
  uint32_t mode;
};

struct pbus_bti {
  uint32_t iommu_index;
  uint32_t bti_id;
};

struct pbus_bootloader_info {
  char vendor[32];
};

// Device metadata to be passed from bootloader via a ZBI record.
struct pbus_boot_metadata {
  // Metadata type (matches `zbi_header_t.type` for bootloader metadata).
  uint32_t zbi_type;
  // Matches `zbi_header_t.extra` for bootloader metadata.
  // Used in cases where bootloader provides multiple metadata records of the same type.
  uint32_t zbi_extra;
};

struct pbus_dev {
  const char* name;
  // `BIND_PLATFORM_DEV_VID`
  uint32_t vid;
  // `BIND_PLATFORM_DEV_PID`
  uint32_t pid;
  // `BIND_PLATFORM_DEV_DID`
  uint32_t did;
  // Instance ID. Contributes to device-name if non-zero.
  // `BIND_PLATFORM_DEV_INSTANCE_ID`
  uint32_t instance_id;
  const pbus_mmio_t* mmio_list;
  size_t mmio_count;
  const pbus_irq_t* irq_list;
  size_t irq_count;
  const pbus_bti_t* bti_list;
  size_t bti_count;
  const pbus_smc_t* smc_list;
  size_t smc_count;
  const pbus_metadata_t* metadata_list;
  size_t metadata_count;
  const pbus_boot_metadata_t* boot_metadata_list;
  size_t boot_metadata_count;
};

// Subset of pdev_board_info_t to be set by the board driver.
struct pbus_board_info {
  // Board name from the boot image platform ID record,
  // (or from the BIOS on x86 platforms).
  char board_name[32];
  // Board specific revision number.
  uint32_t board_revision;
};

struct pbus_protocol_ops {
  zx_status_t (*device_add)(void* ctx, const pbus_dev_t* dev);
  zx_status_t (*protocol_device_add)(void* ctx, uint32_t proto_id, const pbus_dev_t* dev);
  zx_status_t (*register_protocol)(void* ctx, uint32_t proto_id, const uint8_t* protocol_buffer,
                                   size_t protocol_size);
  zx_status_t (*get_board_info)(void* ctx, pdev_board_info_t* out_info);
  zx_status_t (*set_board_info)(void* ctx, const pbus_board_info_t* info);
  zx_status_t (*set_bootloader_info)(void* ctx, const pbus_bootloader_info_t* info);
  zx_status_t (*register_sys_suspend_callback)(void* ctx, const pbus_sys_suspend_t* suspend_cb);
  zx_status_t (*composite_device_add)(void* ctx, const pbus_dev_t* dev, uint64_t fragments,
                                      uint64_t fragments_count, const char* primary_fragment);
  zx_status_t (*add_composite)(void* ctx, const pbus_dev_t* dev, uint64_t fragments,
                               uint64_t fragment_count, const char* primary_fragment);
};

struct pbus_protocol {
  pbus_protocol_ops_t* ops;
  void* ctx;
};

// Helpers

// Adds a new platform device to the bus, using configuration provided by |dev|.
// Platform devices are created in their own separate devhosts.
static inline zx_status_t pbus_device_add(const pbus_protocol_t* proto, const pbus_dev_t* dev) {
  return proto->ops->device_add(proto->ctx, dev);
}

// Adds a device for binding a protocol implementation driver.
// These devices are added in the same devhost as the platform bus.
// After the driver binds to the device it calls `pbus_register_protocol()`
// to register its protocol with the platform bus.
// `pbus_protocol_device_add()` blocks until the protocol implementation driver
// registers its protocol (or times out).
static inline zx_status_t pbus_protocol_device_add(const pbus_protocol_t* proto, uint32_t proto_id,
                                                   const pbus_dev_t* dev) {
  return proto->ops->protocol_device_add(proto->ctx, proto_id, dev);
}

// Called by protocol implementation drivers to register their protocol
// with the platform bus.
static inline zx_status_t pbus_register_protocol(const pbus_protocol_t* proto, uint32_t proto_id,
                                                 const uint8_t* protocol_buffer,
                                                 size_t protocol_size) {
  return proto->ops->register_protocol(proto->ctx, proto_id, protocol_buffer, protocol_size);
}

// Board drivers may use this to get information about the board, and to
// differentiate between multiple boards that they support.
static inline zx_status_t pbus_get_board_info(const pbus_protocol_t* proto,
                                              pdev_board_info_t* out_info) {
  return proto->ops->get_board_info(proto->ctx, out_info);
}

// Board drivers may use this to set information about the board
// (like the board revision number).
// Platform device drivers can access this via `pdev_get_board_info()`.
static inline zx_status_t pbus_set_board_info(const pbus_protocol_t* proto,
                                              const pbus_board_info_t* info) {
  return proto->ops->set_board_info(proto->ctx, info);
}

// Board drivers may use this to set information about the bootloader.
static inline zx_status_t pbus_set_bootloader_info(const pbus_protocol_t* proto,
                                                   const pbus_bootloader_info_t* info) {
  return proto->ops->set_bootloader_info(proto->ctx, info);
}

static inline zx_status_t pbus_register_sys_suspend_callback(const pbus_protocol_t* proto,
                                                             const pbus_sys_suspend_t* suspend_cb) {
  return proto->ops->register_sys_suspend_callback(proto->ctx, suspend_cb);
}

// Deprecated, use AddComposite() instead.
// Adds a composite platform device to the bus. The platform device specified by |dev|
// is the zeroth fragment and the |fragments| array specifies fragments 1 through n.
// The composite device is started in a the driver host of the
// |primary_fragment| if it is specified, or a new driver host if it is is
// NULL. It is not possible to set the primary fragment to "pdev" as that
// would cause the driver to spawn in the platform bus's driver host.
static inline zx_status_t pbus_composite_device_add(const pbus_protocol_t* proto,
                                                    const pbus_dev_t* dev, uint64_t fragments,
                                                    uint64_t fragments_count,
                                                    const char* primary_fragment) {
  return proto->ops->composite_device_add(proto->ctx, dev, fragments, fragments_count,
                                          primary_fragment);
}

// Adds a composite platform device to the bus.
static inline zx_status_t pbus_add_composite(const pbus_protocol_t* proto, const pbus_dev_t* dev,
                                             uint64_t fragments, uint64_t fragment_count,
                                             const char* primary_fragment) {
  return proto->ops->add_composite(proto->ctx, dev, fragments, fragment_count, primary_fragment);
}

__END_CDECLS

#endif  // SRC_DEVICES_BUS_LIB_FUCHSIA_HARDWARE_PLATFORM_BUS_INCLUDE_FUCHSIA_HARDWARE_PLATFORM_BUS_C_BANJO_H_
