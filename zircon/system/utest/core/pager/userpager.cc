// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "userpager.h"

#include <lib/zx/vmar.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <atomic>
#include <cstdio>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>

namespace pager_tests {

bool Vmo::CheckVmar(uint64_t offset, uint64_t len, const void* expected) {
  ZX_ASSERT((offset + len) <= (size_ / ZX_PAGE_SIZE));

  len *= ZX_PAGE_SIZE;
  offset *= ZX_PAGE_SIZE;

  for (uint64_t i = offset / sizeof(uint64_t); i < (offset + len) / sizeof(uint64_t); i++) {
    uint64_t actual_val = base_[i];
    // Make sure we deterministically read from the vmar before reading the
    // expected value, in case things get remapped.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    uint64_t expected_val = expected ? static_cast<const uint64_t*>(expected)[i] : base_val_ + i;
    if (actual_val != expected_val) {
      return false;
    }
  }
  return true;
}

bool Vmo::CheckVmo(uint64_t offset, uint64_t len, const void* expected) {
  len *= ZX_PAGE_SIZE;
  offset *= ZX_PAGE_SIZE;

  zx::vmo tmp_vmo;
  zx_vaddr_t buf = 0;

  zx_status_t status;
  if ((status = zx::vmo::create(len, ZX_VMO_RESIZABLE, &tmp_vmo)) != ZX_OK) {
    fprintf(stderr, "vmo create failed with %s\n", zx_status_get_string(status));
    return false;
  }

  if ((status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, tmp_vmo, 0, len,
                                           &buf)) != ZX_OK) {
    fprintf(stderr, "vmar map failed with %s\n", zx_status_get_string(status));
    return false;
  }

  auto unmap = fbl::MakeAutoCall([&]() { zx_vmar_unmap(zx_vmar_root_self(), buf, len); });

  if (vmo_.read(reinterpret_cast<void*>(buf), offset, len) != ZX_OK) {
    return false;
  }

  for (uint64_t i = 0; i < len / sizeof(uint64_t); i++) {
    auto data_buf = reinterpret_cast<uint64_t*>(buf);
    auto expected_buf = static_cast<const uint64_t*>(expected);
    if (data_buf[i] != (expected ? expected_buf[i] : base_val_ + (offset / sizeof(uint64_t)) + i)) {
      return false;
    }
  }

  return true;
}

bool Vmo::OpRange(uint32_t op, uint64_t offset, uint64_t len) {
  return vmo_.op_range(op, offset * ZX_PAGE_SIZE, len * ZX_PAGE_SIZE, nullptr, 0) == ZX_OK;
}

void Vmo::GenerateBufferContents(void* dest_buffer, uint64_t len, uint64_t paged_vmo_offset) {
  len *= ZX_PAGE_SIZE;
  paged_vmo_offset *= ZX_PAGE_SIZE;
  auto buf = static_cast<uint64_t*>(dest_buffer);
  for (uint64_t idx = 0; idx < len / sizeof(uint64_t); idx++) {
    buf[idx] = base_val_ + (paged_vmo_offset / sizeof(uint64_t)) + idx;
  }
}

std::unique_ptr<Vmo> Vmo::Clone() { return Clone(0, size_); }

std::unique_ptr<Vmo> Vmo::Clone(uint64_t offset, uint64_t size) {
  zx::vmo clone;
  zx_status_t status;
  if ((status = vmo_.create_child(ZX_VMO_CHILD_PRIVATE_PAGER_COPY | ZX_VMO_CHILD_RESIZABLE, offset,
                                  size, &clone)) != ZX_OK) {
    fprintf(stderr, "vmo create_child failed with %s\n", zx_status_get_string(status));
    return nullptr;
  }

  zx_vaddr_t addr;
  if ((status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, clone, 0, size,
                                           &addr)) != ZX_OK) {
    fprintf(stderr, "vmar map failed with %s\n", zx_status_get_string(status));
    return nullptr;
  }

  return std::unique_ptr<Vmo>(new Vmo(std::move(clone), size, reinterpret_cast<uint64_t*>(addr),
                                      addr, base_val_ + (offset / sizeof(uint64_t))));
}

UserPager::UserPager()
    : pager_thread_([this]() -> bool {
        this->PageFaultHandler();
        return true;
      }) {}

UserPager::~UserPager() {
  // If a pager thread was started, gracefully shut it down.
  if (shutdown_event_) {
    shutdown_event_.signal(0, ZX_USER_SIGNAL_0);
    pager_thread_.Wait();
  }
  while (!vmos_.is_empty()) {
    auto vmo = vmos_.pop_front();
    zx::vmar::root_self()->unmap(vmo->base_addr_, vmo->size_);
  }
}

bool UserPager::Init() {
  zx_status_t status;
  if ((status = zx::pager::create(0, &pager_)) != ZX_OK) {
    fprintf(stderr, "pager create failed with %s\n", zx_status_get_string(status));
    return false;
  }
  if ((status = zx::port::create(0, &port_)) != ZX_OK) {
    fprintf(stderr, "port create failed with %s\n", zx_status_get_string(status));
    return false;
  }
  return true;
}

bool UserPager::CreateVmo(uint64_t size, Vmo** vmo_out) {
  if (shutdown_event_) {
    fprintf(stderr, "creating vmo after starting pager thread\n");
    return false;
  }

  zx::vmo vmo;
  size *= ZX_PAGE_SIZE;
  zx_status_t status;
  if ((status = pager_.create_vmo(ZX_VMO_RESIZABLE, port_, next_base_, size, &vmo)) != ZX_OK) {
    fprintf(stderr, "pager create_vmo failed with %s\n", zx_status_get_string(status));
    return false;
  }

  zx_vaddr_t addr;
  if ((status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size,
                                           &addr)) != ZX_OK) {
    fprintf(stderr, "vmar map failed with %s\n", zx_status_get_string(status));
    return false;
  }

  auto paged_vmo = std::unique_ptr<Vmo>(
      new Vmo(std::move(vmo), size, reinterpret_cast<uint64_t*>(addr), addr, next_base_));

  next_base_ += (size / sizeof(uint64_t));

  *vmo_out = paged_vmo.get();
  vmos_.push_back(std::move(paged_vmo));

  return true;
}

bool UserPager::UnmapVmo(Vmo* vmo) {
  zx_status_t status;
  if ((status = zx::vmar::root_self()->unmap(vmo->base_addr_, vmo->size_)) != ZX_OK) {
    fprintf(stderr, "vmar unmap failed with %s\n", zx_status_get_string(status));
    return false;
  }
  return true;
}

bool UserPager::ReplaceVmo(Vmo* vmo, zx::vmo* old_vmo) {
  if (shutdown_event_) {
    fprintf(stderr, "creating vmo after starting pager thread\n");
    return false;
  }

  zx::vmo new_vmo;
  zx_status_t status;
  if ((status = pager_.create_vmo(0, port_, next_base_, vmo->size_, &new_vmo)) != ZX_OK) {
    fprintf(stderr, "pager create_vmo failed with %s\n", zx_status_get_string(status));
    return false;
  }

  zx_info_vmar_t info;
  uint64_t a1, a2;
  if ((status = zx::vmar::root_self()->get_info(ZX_INFO_VMAR, &info, sizeof(info), &a1, &a2)) !=
      ZX_OK) {
    fprintf(stderr, "vmar get_info failed with %s\n", zx_status_get_string(status));
    return false;
  }

  zx_vaddr_t addr;
  if ((status = zx::vmar::root_self()->map(
           ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC_OVERWRITE,
           vmo->base_addr_ - info.base, new_vmo, 0, vmo->size_, &addr)) != ZX_OK) {
    fprintf(stderr, "vmar map failed with %s\n", zx_status_get_string(status));
    return false;
  }
  std::atomic_thread_fence(std::memory_order_seq_cst);

  vmo->base_val_ = next_base_;
  next_base_ += (vmo->size_ / sizeof(uint64_t));

  *old_vmo = std::move(vmo->vmo_);
  vmo->vmo_ = std::move(new_vmo);

  return true;
}

bool UserPager::DetachVmo(Vmo* vmo) {
  zx_status_t status;
  if ((status = pager_.detach_vmo(vmo->vmo())) != ZX_OK) {
    fprintf(stderr, "pager detach_vmo failed with %s\n", zx_status_get_string(status));
    return false;
  }
  return true;
}

void UserPager::ReleaseVmo(Vmo* vmo) {
  if (shutdown_event_) {
    fprintf(stderr, "releasing vmo after starting pager thread\n");
    // Generate an assertion error as there is no return code.
    ZX_ASSERT(!shutdown_event_);
    return;
  }

  zx::vmar::root_self()->unmap(vmo->base_addr_, vmo->size_);
  vmos_.erase(*vmo);
}

bool UserPager::WaitForPageRead(Vmo* vmo, uint64_t offset, uint64_t length, zx_time_t deadline) {
  zx_packet_page_request_t req = {};
  req.command = ZX_PAGER_VMO_READ;
  req.offset = offset * ZX_PAGE_SIZE;
  req.length = length * ZX_PAGE_SIZE;
  return WaitForRequest(vmo->base_val_, req, deadline);
}

bool UserPager::WaitForPageComplete(uint64_t key, zx_time_t deadline) {
  zx_packet_page_request_t req = {};
  req.command = ZX_PAGER_VMO_COMPLETE;
  return WaitForRequest(key, req, deadline);
}

bool UserPager::WaitForRequest(uint64_t key, const zx_packet_page_request& req,
                               zx_time_t deadline) {
  zx_port_packet_t expected = {
      .key = key,
      .type = ZX_PKT_TYPE_PAGE_REQUEST,
      .status = ZX_OK,
      .page_request = req,
  };

  return WaitForRequest(
      [expected](const zx_port_packet& actual) -> bool {
        ZX_ASSERT(expected.type == ZX_PKT_TYPE_PAGE_REQUEST);
        if (expected.key != actual.key || ZX_PKT_TYPE_PAGE_REQUEST != actual.type) {
          return false;
        }
        return memcmp(&expected.page_request, &actual.page_request,
                      sizeof(zx_packet_page_request_t)) == 0;
      },
      deadline);
}

bool UserPager::GetPageReadRequest(Vmo* vmo, zx_time_t deadline, uint64_t* offset,
                                   uint64_t* length) {
  return WaitForRequest(
      [vmo, offset, length](const zx_port_packet& packet) -> bool {
        if (packet.key == vmo->base_val_ && packet.type == ZX_PKT_TYPE_PAGE_REQUEST &&
            packet.page_request.command == ZX_PAGER_VMO_READ) {
          *offset = packet.page_request.offset / ZX_PAGE_SIZE;
          *length = packet.page_request.length / ZX_PAGE_SIZE;
          return true;
        }
        return false;
      },
      deadline);
}

bool UserPager::WaitForRequest(fbl::Function<bool(const zx_port_packet_t& packet)> cmp_fn,
                               zx_time_t deadline) {
  for (auto& iter : requests_) {
    if (cmp_fn(iter.req)) {
      requests_.erase(iter);
      return true;
    }
  }

  zx_time_t now = zx_clock_get_monotonic();
  if (deadline < now) {
    deadline = now;
  }
  while (now <= deadline) {
    zx_port_packet_t actual_packet;
    // TODO: this can block forever if the thread that's
    // supposed to generate the request unexpectedly dies.
    zx_status_t status = port_.wait(zx::time(deadline), &actual_packet);
    if (status == ZX_OK) {
      if (cmp_fn(actual_packet)) {
        return true;
      }

      auto req = std::make_unique<request>();
      req->req = actual_packet;
      requests_.push_front(std::move(req));
    } else {
      // Don't advance now on success, to make sure we read any pending requests
      now = zx_clock_get_monotonic();
    }
  }
  return false;
}

bool UserPager::SupplyPages(Vmo* paged_vmo, uint64_t dest_offset, uint64_t length,
                            uint64_t src_offset) {
  zx::vmo vmo;
  zx_status_t status;
  if ((status = zx::vmo::create((length + src_offset) * ZX_PAGE_SIZE, 0, &vmo)) != ZX_OK) {
    fprintf(stderr, "vmo create failed with %s\n", zx_status_get_string(status));
    return false;
  }

  uint64_t cur = 0;
  while (cur < length) {
    uint8_t data[ZX_PAGE_SIZE];
    paged_vmo->GenerateBufferContents(data, 1, dest_offset + cur);

    if ((status = vmo.write(data, (src_offset + cur) * ZX_PAGE_SIZE, ZX_PAGE_SIZE)) != ZX_OK) {
      fprintf(stderr, "vmo write failed with %s\n", zx_status_get_string(status));
      return false;
    }

    cur++;
  }

  return SupplyPages(paged_vmo, dest_offset, length, std::move(vmo), src_offset);
}

bool UserPager::SupplyPages(Vmo* paged_vmo, uint64_t dest_offset, uint64_t length, zx::vmo src,
                            uint64_t src_offset) {
  zx_status_t status;
  if ((status = pager_.supply_pages(paged_vmo->vmo_, dest_offset * ZX_PAGE_SIZE,
                                    length * ZX_PAGE_SIZE, src, src_offset * ZX_PAGE_SIZE)) !=
      ZX_OK) {
    fprintf(stderr, "pager supply_pages failed with %s\n", zx_status_get_string(status));
    return false;
  }
  return true;
}

bool UserPager::FailPages(Vmo* paged_vmo, uint64_t page_offset, uint64_t page_count,
                          zx_status_t error_status) {
  zx_status_t status;
  if ((status = pager_.op_range(ZX_PAGER_OP_FAIL, paged_vmo->vmo_, page_offset * ZX_PAGE_SIZE,
                                page_count * ZX_PAGE_SIZE, error_status)) != ZX_OK) {
    fprintf(stderr, "pager op_range failed with %s\n", zx_status_get_string(status));
    return false;
  }
  return true;
}

void UserPager::PageFaultHandler() {
  zx::vmo aux_vmo;
  zx_status_t status = zx::vmo::create(ZX_PAGE_SIZE, 0, &aux_vmo);
  ZX_ASSERT(status == ZX_OK);
  while (1) {
    zx_port_packet_t actual_packet;
    status = port_.wait(zx::time::infinite(), &actual_packet);
    if (status != ZX_OK) {
      fprintf(stderr, "Unexpected err %s waiting on port\n", zx_status_get_string(status));
      return;
    }
    if (actual_packet.key == kShutdownKey) {
      ZX_ASSERT(actual_packet.type == ZX_PKT_TYPE_SIGNAL_ONE);
      return;
    }
    ZX_ASSERT(actual_packet.type == ZX_PKT_TYPE_PAGE_REQUEST);
    if (actual_packet.page_request.command == ZX_PAGER_VMO_READ) {
      // Just brute force find matching VMO keys, no need for efficiency.
      for (auto& vmo : vmos_) {
        if (vmo.GetKey() == actual_packet.key) {
          // Supply the requested range.
          SupplyPages(&vmo, actual_packet.page_request.offset / ZX_PAGE_SIZE,
                      actual_packet.page_request.length / ZX_PAGE_SIZE);
        }
      }
    }
  }
}

bool UserPager::StartTaggedPageFaultHandler() {
  if (shutdown_event_) {
    fprintf(stderr, "Page fault handler already created\n");
    return false;
  }
  zx_status_t status;
  status = zx::event::create(0, &shutdown_event_);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to create event for shutdown sycnronization\n");
    return false;
  }

  status = shutdown_event_.wait_async(port_, kShutdownKey, ZX_USER_SIGNAL_0, 0);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to associate shutdown event with port\n");
    return false;
  }

  if (!pager_thread_.Start()) {
    fprintf(stderr, "Failed to start page fault handling thread\n");
    return false;
  }
  return true;
}

}  // namespace pager_tests
