// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/pager_thread_pool.h"

#include <zircon/assert.h>
#include <zircon/syscalls-next.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/types.h>
#include <zircon/threads.h>

#include <fbl/auto_lock.h>

#include "src/lib/storage/vfs/cpp/paged_vfs.h"

namespace fs {

PagerThreadPool::PagerThreadPool(PagedVfs& vfs, int num_threads)
    : vfs_(vfs), num_threads_(num_threads) {}

PagerThreadPool::~PagerThreadPool() {
  // The loop will treat a USER packet as the quit event so we can synchronize with it.
  zx_port_packet_t quit_packet{};
  quit_packet.type = ZX_PKT_TYPE_USER;

  // Each thread will quit as soon as it reads one quit packet, so post that many packets.
  for (int i = 0; i < num_threads_; i++)
    port_.queue(&quit_packet);

  for (auto& thread : threads_) {
    thread.join();
  }
  threads_.clear();
}

zx::result<> PagerThreadPool::Init() {
  if (zx_status_t status = zx::port::create(0, &port_); status != ZX_OK)
    return zx::error(status);

  // Start all the threads.
  for (int i = 0; i < num_threads_; i++)
    threads_.push_back(std::thread([self = this]() { self->ThreadProc(); }));

  return zx::ok();
}

std::vector<zx::unowned_thread> PagerThreadPool::GetPagerThreads() const {
  std::vector<zx::unowned_thread> result;
  result.reserve(num_threads_);

  for (const auto& thread : threads_) {
    result.emplace_back(
        native_thread_get_zx_handle(const_cast<std::thread&>(thread).native_handle()));
  }
  return result;
}

void PagerThreadPool::ThreadProc() {
  while (true) {
    zx_port_packet_t packet;
    if (zx_status_t status = port_.wait(zx::time::infinite(), &packet); status != ZX_OK) {
      // TODO(brettw) it would be nice to log from here but some drivers that depend on this
      // library aren't allowed to log.
      // FX_LOGST(ERROR, "pager") << "Pager port wait failed, stopping. The system will probably go
      // down.";
      return;
    }

    if (packet.type == ZX_PKT_TYPE_USER)
      break;  // USER packets tell us to quit.

    // Should only be getting pager requests on this port.
    ZX_ASSERT(packet.type == ZX_PKT_TYPE_PAGE_REQUEST);

    switch (packet.page_request.command) {
      case ZX_PAGER_VMO_READ:
        vfs_.PagerVmoRead(packet.key, packet.page_request.offset, packet.page_request.length);
        break;
      case ZX_PAGER_VMO_DIRTY:
        vfs_.PagerVmoDirty(packet.key, packet.page_request.offset, packet.page_request.length);
        break;
      case ZX_PAGER_VMO_COMPLETE:
        // We don't currently do anything on "complete" requests. This is issued by the kernel in
        // response to a "detach vmo" call. But with multiple pager threads in the thread pool,
        // we have no guarantee that we'll process the complete message after the read requests
        // that were already pending, so the "complete" message doesn't tell us anything.
        //
        // We rely on the fact that by the time the kernel returns from the "detach" request that
        // no more valid page requests are issued, and that any in-flight ones (which might be
        // pending in our port queue or are being handled in the filesystem) are internally
        // cancelled by the kernel. As such, as long as we can tolerate pager requests for detached
        // vmos (which we do by using unique identifiers into a map), there is no need to handle
        // the COMPLETE message.
        break;
      default:
        // Unexpected request.
        ZX_ASSERT(false);
        break;
    }
  }
}

}  // namespace fs
