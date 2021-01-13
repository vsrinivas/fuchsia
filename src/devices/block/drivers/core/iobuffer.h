// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_CORE_IOBUFFER_H_
#define SRC_DEVICES_BLOCK_DRIVERS_CORE_IOBUFFER_H_

#include <fuchsia/hardware/block/c/banjo.h>
#include <lib/zx/vmo.h>

#include <fbl/intrusive_wavl_tree.h>
#include <fbl/ref_counted.h>

// Represents the mapping of "vmoid --> VMO"
class IoBuffer : public fbl::WAVLTreeContainable<fbl::RefPtr<IoBuffer>>,
                 public fbl::RefCounted<IoBuffer> {
 public:
  vmoid_t GetKey() const { return vmoid_; }

  // TODO(smklein): This function is currently labelled 'hack' since we have
  // no way to ensure that the size of the VMO won't change in between
  // checking it and using it.  This will require a mechanism to "pin" VMO pages.
  // The units of length and vmo_offset is bytes.
  zx_status_t ValidateVmoHack(uint64_t length, uint64_t vmo_offset);

  zx_handle_t vmo() const { return io_vmo_.get(); }

  IoBuffer(zx::vmo vmo, vmoid_t vmoid);
  ~IoBuffer();

 private:
  friend struct TypeWAVLTraits;
  DISALLOW_COPY_ASSIGN_AND_MOVE(IoBuffer);

  const zx::vmo io_vmo_;
  const vmoid_t vmoid_;
};

#endif  // SRC_DEVICES_BLOCK_DRIVERS_CORE_IOBUFFER_H_
