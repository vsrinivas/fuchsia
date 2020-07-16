// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_SYSCALLS_PORT_H_
#define SYSROOT_ZIRCON_SYSCALLS_PORT_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// clang-format off

// zx_object_wait_async() options
// Do not use ZX_WAIT_ASYNC_ONCE. It is now superfluous and will be removed.
#define ZX_WAIT_ASYNC_ONCE          ((uint32_t)0u)
#define ZX_WAIT_ASYNC_TIMESTAMP     ((uint32_t)1u)

// packet types.  zx_port_packet_t::type
#define ZX_PKT_TYPE_USER            ((uint8_t)0x00u)
#define ZX_PKT_TYPE_SIGNAL_ONE      ((uint8_t)0x01u)
// 0x02 was previously used for "ZX_PKT_TYPE_SIGNAL_REP".
#define ZX_PKT_TYPE_GUEST_BELL      ((uint8_t)0x03u)
#define ZX_PKT_TYPE_GUEST_MEM       ((uint8_t)0x04u)
#define ZX_PKT_TYPE_GUEST_IO        ((uint8_t)0x05u)
#define ZX_PKT_TYPE_GUEST_VCPU      ((uint8_t)0x06u)
#define ZX_PKT_TYPE_INTERRUPT       ((uint8_t)0x07u)
#define ZX_PKT_TYPE_PAGE_REQUEST    ((uint8_t)0x09u)

// For options passed to port_create
#define ZX_PORT_BIND_TO_INTERRUPT   ((uint32_t)(0x1u << 0))

#define ZX_PKT_TYPE_MASK            ((uint32_t)0x000000FFu)

#define ZX_PKT_IS_USER(type)          ((type) == ZX_PKT_TYPE_USER)
#define ZX_PKT_IS_SIGNAL_ONE(type)    ((type) == ZX_PKT_TYPE_SIGNAL_ONE)
#define ZX_PKT_IS_GUEST_BELL(type)    ((type) == ZX_PKT_TYPE_GUEST_BELL)
#define ZX_PKT_IS_GUEST_MEM(type)     ((type) == ZX_PKT_TYPE_GUEST_MEM)
#define ZX_PKT_IS_GUEST_IO(type)      ((type) == ZX_PKT_TYPE_GUEST_IO)
#define ZX_PKT_IS_GUEST_VCPU(type)    ((type) == ZX_PKT_TYPE_GUEST_VCPU)
#define ZX_PKT_IS_INTERRUPT(type)     ((type) == ZX_PKT_TYPE_INTERRUPT)
#define ZX_PKT_IS_PAGE_REQUEST(type)  ((type) == ZX_PKT_TYPE_PAGE_REQUEST)

// zx_packet_guest_vcpu_t::type
#define ZX_PKT_GUEST_VCPU_INTERRUPT  ((uint8_t)0)
#define ZX_PKT_GUEST_VCPU_STARTUP    ((uint8_t)1)

// zx_packet_page_request_t::command
#define ZX_PAGER_VMO_READ ((uint16_t) 0)
#define ZX_PAGER_VMO_COMPLETE ((uint16_t) 1)
// clang-format on

// port_packet_t::type ZX_PKT_TYPE_USER.
typedef union zx_packet_user {
  uint64_t u64[4];
  uint32_t u32[8];
  uint16_t u16[16];
  uint8_t c8[32];
} zx_packet_user_t;

// port_packet_t::type ZX_PKT_TYPE_SIGNAL_ONE.
typedef struct zx_packet_signal {
  zx_signals_t trigger;
  zx_signals_t observed;
  uint64_t count;
  uint64_t timestamp;
  uint64_t reserved1;
} zx_packet_signal_t;

typedef struct zx_packet_guest_bell {
  zx_gpaddr_t addr;
  uint64_t reserved0;
  uint64_t reserved1;
  uint64_t reserved2;
} zx_packet_guest_bell_t;

typedef struct zx_packet_guest_mem {
  zx_gpaddr_t addr;
#if __aarch64__
  uint8_t access_size;
  bool sign_extend;
  uint8_t xt;
  bool read;
  uint8_t padding1[4];
  uint64_t data;
  uint64_t reserved;
#elif __x86_64__
// NOTE: x86 instructions are guaranteed to be 15 bytes or fewer.
#define X86_MAX_INST_LEN 15u
  uint8_t inst_len;
  uint8_t inst_buf[X86_MAX_INST_LEN];
  // This is the default operand size as determined by the CS and EFER register (Volume 3,
  // Section 5.2.1). If operating in 64-bit mode then near branches and all instructions, except
  // far branches, that implicitly reference the RSP will actually have a default operand size of
  // 64-bits (Volume 2, Section 2.2.1.7), and not the 32-bits that will be given here.
  uint8_t default_operand_size;
  uint8_t reserved[7];
#endif
} zx_packet_guest_mem_t;

typedef struct zx_packet_guest_io {
  uint16_t port;
  uint8_t access_size;
  bool input;
  union {
    struct {
      uint8_t u8;
      uint8_t padding1[3];
    };
    struct {
      uint16_t u16;
      uint8_t padding2[2];
    };
    uint32_t u32;
    uint8_t data[4];
  };
  uint64_t reserved0;
  uint64_t reserved1;
  uint64_t reserved2;
} zx_packet_guest_io_t;

typedef struct zx_packet_guest_vcpu {
  union {
    struct {
      uint64_t mask;
      uint8_t vector;
      uint8_t padding1[7];
    } interrupt;
    struct {
      uint64_t id;
      zx_gpaddr_t entry;
    } startup;
  };
  uint8_t type;
  uint8_t padding1[7];
  uint64_t reserved;
} zx_packet_guest_vcpu_t;

typedef struct zx_packet_interrupt {
  zx_time_t timestamp;
  uint64_t reserved0;
  uint64_t reserved1;
  uint64_t reserved2;
} zx_packet_interrupt_t;

typedef struct zx_packet_page_request {
  uint16_t command;
  uint16_t flags;
  uint32_t reserved0;
  uint64_t offset;
  uint64_t length;
  uint64_t reserved1;
} zx_packet_page_request_t;

typedef struct zx_port_packet {
  uint64_t key;
  uint32_t type;
  zx_status_t status;
  union {
    zx_packet_user_t user;
    zx_packet_signal_t signal;
    zx_packet_guest_bell_t guest_bell;
    zx_packet_guest_mem_t guest_mem;
    zx_packet_guest_io_t guest_io;
    zx_packet_guest_vcpu_t guest_vcpu;
    zx_packet_interrupt_t interrupt;
    zx_packet_page_request_t page_request;
  };
} zx_port_packet_t;

__END_CDECLS

#endif  // SYSROOT_ZIRCON_SYSCALLS_PORT_H_
