// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_JOB_H_
#define LIB_ZX_JOB_H_

#include <lib/zx/task.h>
#include <lib/zx/process.h>

namespace zx {

class process;

class job final : public task<job> {
 public:
  static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_JOB;

  constexpr job() = default;

  explicit job(zx_handle_t value) : task(value) {}

  explicit job(handle&& h) : task(h.release()) {}

  job(job&& other) : task(other.release()) {}

  job& operator=(job&& other) {
    reset(other.release());
    return *this;
  }

  static zx_status_t create(const zx::job& parent, uint32_t options, job* result);

  // Provide strongly-typed overloads, in addition to get_child(handle*).
  using task<job>::get_child;
  zx_status_t get_child(uint64_t koid, zx_rights_t rights, job* result) const {
    // Allow for |result| and |this| aliasing the same container.
    job h;
    zx_status_t status = zx_object_get_child(value_, koid, rights, h.reset_and_get_address());
    result->reset(h.release());
    return status;
  }
  zx_status_t get_child(uint64_t koid, zx_rights_t rights, process* result) const;

  zx_status_t set_policy(uint32_t options, uint32_t topic, const void* policy,
                         uint32_t count) const {
    return zx_job_set_policy(get(), options, topic, policy, count);
  }

  zx_status_t set_critical(uint32_t options, const zx::process& process) const {
    return zx_job_set_critical(get(), options, process.get());
  }

  // Ideally this would be called zx::job::default(), but default is a
  // C++ keyword and cannot be used as a function name.
  static inline unowned<job> default_job() { return unowned<job>(zx_job_default()); }
};

using unowned_job = unowned<job>;

}  // namespace zx

#endif  // LIB_ZX_JOB_H_
