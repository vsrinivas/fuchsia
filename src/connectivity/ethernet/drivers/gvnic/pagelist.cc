// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/ethernet/drivers/gvnic/pagelist.h"

#include "src/connectivity/ethernet/drivers/gvnic/bigendian.h"

uint32_t PageList::next_id_ = 0;

PageList::PageList(std::unique_ptr<dma_buffer::BufferFactory>& buffer_factory, zx::bti& bti,
                   std::unique_ptr<dma_buffer::ContiguousBuffer>& scratch_page) {
  zx_status_t status;
  id_ = next_id_++;
  // TODO(charlieross): Once the requirements for these pagelists are better understood, support
  // making smaller pagelists. But for now, make em big.
  length_ = static_cast<uint32_t>(scratch_page->size()) /
            sizeof(uint64_t);  // Make it as large as we can.
  status = buffer_factory->CreatePaged(bti, zx_system_get_page_size() * length_,
                                       /*enable_cache*/ true, &pages_);
  ZX_ASSERT_MSG(status == ZX_OK,
                "buffer_factory->CreatePaged(bti, zx_system_get_page_size() * length_, true, "
                "&pages_): FAILED (%s)",
                zx_status_get_string(status));
  // Can't just memcpy. Need to assign individually to get the BigEndian goodness.
  auto const pl_addrs_tgt = reinterpret_cast<BigEndian<uint64_t>*>(scratch_page->virt());
  auto const pl_addrs_src = pages_->phys();
  for (uint32_t i = 0; i < length_; i++) {
    pl_addrs_tgt[i] = pl_addrs_src[i];
  }
}

uint32_t PageList::id() { return id_; }
uint32_t PageList::length() { return length_; }
const dma_buffer::PagedBuffer* PageList::pages() { return pages_.get(); }
