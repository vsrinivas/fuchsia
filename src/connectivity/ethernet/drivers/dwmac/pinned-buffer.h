// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_DWMAC_PINNED_BUFFER_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_DWMAC_PINNED_BUFFER_H_

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/bti.h>
#include <lib/zx/pmt.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/debug.h>
#include <fbl/ref_ptr.h>

class PinnedBuffer : public fbl::RefCounted<PinnedBuffer> {
 public:
  static fbl::RefPtr<PinnedBuffer> Create(size_t size, const zx::bti& bti, uint32_t cache_policy);
  zx_status_t LookupPhys(zx_off_t offset, zx_paddr_t* out);

  void* GetBaseAddress() { return vmo_mapper_.start(); }
  size_t GetSize() { return vmo_mapper_.size(); }
  zx_status_t UnPin();

 private:
  PinnedBuffer() = default;
  // Note - not using zx:bti since this bti may be used for multiple

  fzl::VmoMapper vmo_mapper_;
  zx::vmo vmo_;
  zx::pmt pmt_;

  std::unique_ptr<zx_paddr_t[]> paddrs_;
};

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_DWMAC_PINNED_BUFFER_H_
