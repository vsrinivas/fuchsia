// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/paged_vmo.h>
#include <zircon/assert.h>

#include <utility>

namespace async {

PagedVmoBase::PagedVmoBase(async_paged_vmo_handler_t* handler)
    : paged_vmo_{{ASYNC_STATE_INIT}, handler, ZX_HANDLE_INVALID, ZX_HANDLE_INVALID} {
  ZX_DEBUG_ASSERT(handler);
}

PagedVmoBase::~PagedVmoBase() { Detach(); }

zx_status_t PagedVmoBase::CreateVmo(async_dispatcher_t* dispatcher, zx::unowned_pager pager,
                                    uint32_t options, uint64_t vmo_size, zx::vmo* vmo_out) {
  if (dispatcher_) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  zx_status_t status = async_create_paged_vmo(dispatcher, &paged_vmo_, options, pager->get(),
                                              vmo_size, vmo_out->reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }
  dispatcher_ = dispatcher;
  paged_vmo_.pager = pager->get();
  paged_vmo_.vmo = vmo_out->get();
  return ZX_OK;
}

zx_status_t PagedVmoBase::Detach() {
  if (!dispatcher_) {
    return ZX_ERR_NOT_FOUND;
  }

  auto dispatcher = dispatcher_;
  dispatcher_ = nullptr;

  zx_status_t status = async_detach_paged_vmo(dispatcher, &paged_vmo_);
  // |dispatcher| is required to be single-threaded, Detach() is only supposed to be called on
  // |dispatcher|'s thread, and we verified that the port was bound before calling
  // async_detach_paged_vmo().
  ZX_DEBUG_ASSERT(status != ZX_ERR_NOT_FOUND);
  return status;
}

PagedVmo::PagedVmo(Handler handler)
    : PagedVmoBase(&PagedVmo::CallHandler), handler_(std::move(handler)) {}

PagedVmo::~PagedVmo() = default;

void PagedVmo::CallHandler(async_dispatcher_t* dispatcher, async_paged_vmo_t* paged_vmo,
                           zx_status_t status, const zx_packet_page_request_t* request) {
  auto self = Dispatch<PagedVmo>(paged_vmo, status);
  self->handler_(dispatcher, self, status, request);
}

}  // namespace async
