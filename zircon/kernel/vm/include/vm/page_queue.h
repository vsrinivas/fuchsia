// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUE_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUE_H_

// Specifies the indices for both the PageQueues::page_queues_ and the
// PageQueues::page_queue_counts_.  Also used by page.h, so in separate header.
enum PageQueue : uint32_t {
  // The number of pager backed queues is slightly arbitrary, but to be useful you want at least 3
  // representing
  //  * Very new pages that you probably don't want to evict as doing so probably implies you are in
  //    swap death
  //  * Slightly old pages that could be evicted if needed
  //  * Very old pages that you'd be happy to evict
  // For now 4 queues are chosen to stretch out that middle group such that the distinction between
  // slightly old and very old is more pronounced.
  kNumPagerBacked = 4,

  PageQueueNone = 0,
  PageQueueUnswappable,
  PageQueueWired,
  PageQueueUnswappableZeroFork,
  PageQueuePagerBackedInactive,
  PageQueuePagerBackedBase,
  PageQueuePagerBackedLast = PageQueuePagerBackedBase + kNumPagerBacked - 1,
  PageQueueNumQueues,
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_QUEUE_H_
