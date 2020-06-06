// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-object/object.h>
#include <lib/fake-resource/resource.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/resource.h>

#include <array>
#include <utility>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

#include "zircon/errors.h"
#include "zircon/syscalls/object.h"

namespace {

// Implement a basic fake Resource object to use with accompanying syscalls.
// This object will only be to spec in regards to having a |kind| and inclusive
// range. Only shared resources are permitted at this time to reduce complexity
// as exclusive resources are not needed for most test purposes. It is not permitted
// to create a |root| resource through this interface.
class Resource final : public Object {
 public:
  virtual ~Resource() = default;

  static zx_status_t Create(zx_paddr_t base, size_t size, zx_rsrc_kind_t kind,
                            zx_rsrc_flags_t flags, const char* name, size_t name_len,
                            fbl::RefPtr<Object>* out) {
    *out = fbl::AdoptRef(new Resource(base, size, kind, flags, name, name_len));
    return ZX_OK;
  }

  zx_status_t get_info(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                       size_t* actual_count, size_t* avail_count) override;

  HandleType type() const final { return HandleType::RESOURCE; }
  zx_paddr_t base() const { return base_; }
  size_t size() const { return size_; }
  zx_rsrc_kind_t kind() const { return kind_; }
  bool is_exclusive() const { return is_exclusive_; }

 private:
  zx_paddr_t base_;
  size_t size_;
  zx_rsrc_kind_t kind_;
  const bool is_exclusive_;
  std::array<char, ZX_MAX_NAME_LEN> name_;

  Resource(zx_paddr_t base, size_t size, zx_rsrc_kind_t kind, zx_rsrc_flags_t flags,
           const char* name, size_t name_len)
      : base_(base), size_(size), kind_(kind), is_exclusive_(flags & ZX_RSRC_FLAG_EXCLUSIVE) {
    ZX_ASSERT_MSG(kind_ != ZX_RSRC_KIND_IRQ && kind_ != ZX_RSRC_KIND_HYPERVISOR &&
                      kind_ != ZX_RSRC_KIND_VMEX && kind_ != ZX_RSRC_KIND_SMC,
                  "fake-resource: unsupported kind: %u\n", kind);
    memcpy(name_.data(), name, name_len);
  }
};

// Returns true if r2 is valid within r1
bool is_valid_range(zx_paddr_t r1_base, size_t r1_size, zx_paddr_t r2_base, size_t r2_size) {
  return (r2_base >= r1_base && r2_base + r2_size <= r1_base + r1_size);
}

bool exclusive_region_overlaps(zx_rsrc_kind_t kind, zx_paddr_t new_rsrc_base,
                               size_t new_rsrc_size) {
  bool overlaps = false;
  zx_paddr_t new_rsrc_end = new_rsrc_base + new_rsrc_size;
  FakeHandleTable().ForEach(HandleType::RESOURCE, [&](Object* obj) -> bool {
    auto* rsrc = static_cast<Resource*>(obj);
    // In the case of exclusive resources we need to ensure the new resource does not
    // overlap with existing exclusive ranges.
    if (rsrc->kind() == kind && rsrc->is_exclusive()) {
      zx_paddr_t rsrc_end = rsrc->base() + rsrc->size();
      // If we overlap from the base side of the resource
      if ((new_rsrc_base <= rsrc->base() && new_rsrc_end > rsrc->base()) ||
          // If we start inside the existing resource
          (new_rsrc_base >= rsrc->base() && new_rsrc_base <= rsrc_end)) {
        overlaps = true;
        return false;
      }
    }
    return true;
  });
  return overlaps;
}

}  // namespace

// Implements fake-resources version of |zx_object_get_info|.
zx_status_t Resource::get_info(zx_handle_t handle, uint32_t topic, void* buffer, size_t buffer_size,
                               size_t* actual_count, size_t* avail_count) {
  ZX_ASSERT_MSG(topic == ZX_INFO_RESOURCE, "fake_resource_get_info: wrong topic type: %u\n", topic);
  ZX_ASSERT_MSG(buffer_size >= sizeof(zx_info_resource_t),
                "fake_resource_get_info: info buffer is too small (actual: %zu, needed: %zu)\n",
                buffer_size, sizeof(zx_info_resource_t));

  auto* info = static_cast<zx_info_resource_t*>(buffer);
  info->base = base_;
  info->size = size_;
  info->kind = kind_;
  memcpy(info->name, name_.data(), name_.size());
  info->flags = 0;

  if (actual_count) {
    *actual_count = 1;
  }

  if (avail_count) {
    *avail_count = 1;
  }

  return ZX_OK;
}

__BEGIN_CDECLS

__EXPORT
zx_status_t zx_resource_create(zx_handle_t parent_rsrc, uint32_t options, uint64_t base,
                               size_t size, const char* name, size_t name_size,
                               zx_handle_t* resource_out) {
  zx::status get_res = FakeHandleTable().Get(parent_rsrc);
  if (!get_res.is_ok()) {
    return get_res.status_value();
  }
  fbl::RefPtr<Resource> parent = fbl::RefPtr<Resource>::Downcast(std::move(get_res.value()));

  // Fake root resources have no range or kind verification necessary.
  zx_rsrc_kind_t kind = ZX_RSRC_EXTRACT_KIND(options);
  if (parent->kind() != ZX_RSRC_KIND_ROOT) {
    if (kind != parent->kind()) {
      return ZX_ERR_WRONG_TYPE;
    }
    // Ensure the child range fits within the parent.
    if (!is_valid_range(parent->base(), parent->size(), base, size)) {
      return ZX_ERR_ACCESS_DENIED;
    }
  }

  // Ensure that if this region is exclusive it does not overlap with an exclusive region.
  zx_rsrc_flags_t flags = ZX_RSRC_EXTRACT_FLAGS(options);
  if ((flags & ZX_RSRC_FLAG_EXCLUSIVE) && exclusive_region_overlaps(kind, base, size)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  fbl::RefPtr<Object> new_res;
  ZX_ASSERT(Resource::Create(base, size, kind, flags, name, name_size, &new_res) == ZX_OK);
  zx::status add_res = FakeHandleTable().Add(std::move(new_res));
  if (add_res.is_ok()) {
    *resource_out = add_res.value();
  }
  return add_res.status_value();
}

// Create a paged VMO to stand in for a physical one for tests. The real
// zx_vmo_set_cache_policy can still be called on a paged VMO so there's no need
// to replace that syscall with a fake.
__EXPORT
zx_status_t zx_vmo_create_physical(zx_handle_t handle, zx_paddr_t paddr, size_t size,
                                   zx_handle_t* out) {
  zx::status get_res = FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    return get_res.status_value();
  }
  fbl::RefPtr<Resource> resource = fbl::RefPtr<Resource>::Downcast(std::move(get_res.value()));

  if (!is_valid_range(resource->base(), resource->size(), paddr, size)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  return zx_vmo_create(size, 0, out);
}

// Validate the syscall but otherwise don't take action. If a test actually
// needs IO permissions then more work will need to be done getting them real
// resources to allwow it.
zx_status_t ioport_syscall_common(zx_handle_t handle, uint16_t io_addr, uint32_t len) {
  zx::status get_res = FakeHandleTable().Get(handle);
  if (!get_res.is_ok()) {
    return get_res.status_value();
  }
  fbl::RefPtr<Resource> resource = fbl::RefPtr<Resource>::Downcast(std::move(get_res.value()));

  if (resource->kind() != ZX_RSRC_KIND_IOPORT) {
    return ZX_ERR_WRONG_TYPE;
  }

  if (!is_valid_range(resource->base(), resource->size(), io_addr, len)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  return ZX_OK;
}

__EXPORT
zx_status_t zx_ioports_request(zx_handle_t resource, uint16_t io_addr, uint32_t len) {
  return ioport_syscall_common(resource, io_addr, len);
}

// Same as zx_ioports_request
__EXPORT
zx_status_t zx_ioports_release(zx_handle_t resource, uint16_t io_addr, uint32_t len) {
  return ioport_syscall_common(resource, io_addr, len);
}

// The root resource is handed off to userboot by the kernel and is not something that can be
// created in userspace normally. This allows a test to bootstrap a resource chain by creating
// a fake root resoure.
__EXPORT
zx_status_t fake_root_resource_create(zx_handle_t* out) {
  std::array<char, ZX_MAX_NAME_LEN> name = {"FAKE ROOT"};
  fbl::RefPtr<Object> new_res;
  ZX_ASSERT(Resource::Create(0, 0, ZX_RSRC_KIND_ROOT, 0, name.data(), name.size(), &new_res) ==
            ZX_OK);
  zx::status add_res = FakeHandleTable().Add(std::move(new_res));
  if (add_res.is_ok()) {
    *out = add_res.value();
  }
  return add_res.status_value();
}

__END_CDECLS
