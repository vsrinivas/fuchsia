// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_PAGELIST_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_PAGELIST_H_

#include <lib/dma-buffer/buffer.h>
#include <zircon/status.h>

class PageList {
 public:
  PageList(std::unique_ptr<dma_buffer::BufferFactory>& buffer_factory, zx::bti& bti,
           std::unique_ptr<dma_buffer::ContiguousBuffer>& scratch_page);

  uint32_t id();
  uint32_t length();
  const dma_buffer::PagedBuffer* pages();

 private:
  // No mutex needed for this. This is only ever read or written by one thread (the main thread).
  static uint32_t next_id_;

  uint32_t id_;
  uint32_t length_;

  // The actual pages that are are allocated for transferring data:
  std::unique_ptr<dma_buffer::PagedBuffer> pages_;
};

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_GVNIC_PAGELIST_H_
