// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_PROFILE_PROVIDER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_PROFILE_PROVIDER_H_

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <cstdint>
#include <unordered_set>

namespace media::audio {

class FakeProfileProvider : public fuchsia::scheduler::ProfileProvider {
 public:
  fidl::InterfaceRequestHandler<fuchsia::scheduler::ProfileProvider> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // |GetProfile| will return ZX_ERR_NOT_FOUND/ZX_HANDLE_INVALID for any priority that has not
  // previously been marked as valid with a call to |SetProfile|.
  //
  // Note that currently the only works for a single |GetProfile| call since we don't duplicate
  // a new handle before sending it back to the client.
  bool SetProfile(uint32_t priority) { return valid_priorities_.insert(priority).second; }

 private:
  // |fuchsia::scheduler::ProfileProvider|
  void GetProfile(uint32_t priority, std::string name, GetProfileCallback callback) override {
    auto it = valid_priorities_.find(priority);
    if (it == valid_priorities_.end()) {
      callback(ZX_ERR_NOT_FOUND, zx::profile());
    } else {
      callback(ZX_OK, zx::profile());
    }
  }

  // |fuchsia::scheduler::ProfileProvider|
  // TODO(eieio): Temporary until the deadline scheduler fully lands in tree.
  void GetDeadlineProfile(uint64_t capacity, uint64_t deadline, uint64_t period, std::string name,
                          GetProfileCallback callback) override {
    // This will fail if used (ex: zx_object_set_profile), but allows us to do some testing of the
    // consuming code.
    callback(ZX_OK, zx::profile());
  }

  // |fuchsia::scheduler::ProfileProvider|
  void GetCpuAffinityProfile(fuchsia::scheduler::CpuSet cpu_mask,
                             GetProfileCallback callback) override {}

  // |fuchsia::scheduler::ProfileProvider|
  void SetProfileByRole(zx::thread thread, std::string role,
                        SetProfileByRoleCallback callback) override {
    callback(ZX_OK);
  }

  std::unordered_set<uint32_t> valid_priorities_;
  fidl::BindingSet<fuchsia::scheduler::ProfileProvider> bindings_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_FAKE_PROFILE_PROVIDER_H_
