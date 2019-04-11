// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_IO_H_
#define GARNET_BIN_GUEST_VMM_IO_H_

#include <lib/async/cpp/trap.h>
#include <trace/event.h>
#include <zircon/types.h>

class Guest;

struct IoValue {
  uint8_t access_size;
  union {
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    uint8_t data[8];
  };
};

// Callback interface to be implemented by devices.
//
// IoHandlers may be called from multiple VCPU threads concurrently so
// implementations must implement proper internal synchronization.
class IoHandler {
 public:
  virtual ~IoHandler() = default;

  // Read |value.access_size| bytes from |addr| into |value|.
  virtual zx_status_t Read(zx_gpaddr_t addr, IoValue* value) const = 0;

  // Write |value.access_size| bytes to |addr| from |value|.
  virtual zx_status_t Write(zx_gpaddr_t addr, const IoValue& value) = 0;
};

// Represents a single mapping of an |IoHandler| to an address range.
//
// A single handler may be mapped to multiple distinct address ranges.
class IoMapping {
 public:
  static IoMapping* FromPortKey(zx_gpaddr_t key) {
    return reinterpret_cast<IoMapping*>(key);
  }

  // Constructs an IoMapping.
  //
  // Any accesses starting at |base| for |size| bytes are to be handled by
  // |handler|. When invoking |handler| the address is provides as relative to
  // |base|. Additionally an |off| can also be provided to add a displacement
  // into |handler|.
  //
  // Specifically, an access to |base| would invoke the |handler| with the
  // address |off| and increase linearly from there with additional displacement
  // into |base|. This implies that |handler| should be prepared handle accesses
  // between |off| (inclusive) and |off| + |size| (exclusive).
  IoMapping(uint32_t kind, zx_gpaddr_t base, size_t size, zx_gpaddr_t off,
            IoHandler* handler);

  zx_gpaddr_t base() const {
    return base_;
  }

  size_t size() const {
    return size_;
  }

  uint32_t kind() const {
    return kind_;
  }

  zx_status_t Read(zx_gpaddr_t addr, IoValue* value) const {
    const zx_gpaddr_t address = addr - base_ + off_;
    TRACE_DURATION("machina", "read", "address", address, "access_size",
                   value->access_size);
    return handler_->Read(address, value);
  }

  zx_status_t Write(zx_gpaddr_t addr, const IoValue& value) {
    const zx_gpaddr_t address = addr - base_ + off_;
    TRACE_DURATION("machina", "write", "address", address, "access_size",
                   value.access_size);
    return handler_->Write(address, value);
  }

  zx_status_t SetTrap(Guest* guest, async_dispatcher_t* dispatcher);

 private:
  void CallIoHandlerAsync(async_dispatcher_t* dispatcher,
                          async::GuestBellTrapBase* trap, zx_status_t status,
                          const zx_packet_guest_bell_t* bell);

  const uint32_t kind_;
  const zx_gpaddr_t base_;
  const size_t size_;
  const zx_gpaddr_t off_;
  IoHandler* handler_;
  async::GuestBellTrapMethod<IoMapping, &IoMapping::CallIoHandlerAsync>
      async_trap_;
};

#endif  // GARNET_BIN_GUEST_VMM_IO_H_
