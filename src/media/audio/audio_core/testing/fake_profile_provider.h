// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_PROFILE_PROVIDER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_PROFILE_PROVIDER_H_

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <cstdint>
#include <unordered_map>

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
  bool SetProfile(uint32_t priority) {
    // Since there's no easy way to create a profile handle in a test context, we'll just use an
    // event handle. This will be sufficient to allow the handle to be sent over the channel back
    // to the caller, but it will obviously not work of the caller is doing anything that requires
    // a zx::profile. This limitation is sufficient for the purposes of our tests.
    zx::event e;
    zx::event::create(0, &e);
    return profiles_by_priority_.insert({priority, zx::profile(e.release())}).second;
  }

 private:
  // |fuchsia::scheduler::ProfileProvider|
  void GetProfile(uint32_t priority, std::string name, GetProfileCallback callback) override {
    auto it = profiles_by_priority_.find(priority);
    if (it == profiles_by_priority_.end()) {
      callback(ZX_ERR_NOT_FOUND, zx::profile());
    } else {
      callback(ZX_OK, std::move(it->second));
    }
  }

  // |fuchsia::scheduler::ProfileProvider|
  // TODO(eieio): Temporary until the deadline scheduler fully lands in tree.
  void GetDeadlineProfile(uint64_t capacity, uint64_t deadline, uint64_t period, std::string name,
                          GetProfileCallback callback) override {}

  // |fuchsia::scheduler::ProfileProvider|
  void GetCpuAffinityProfile(fuchsia::scheduler::CpuSet cpu_mask,
                             GetProfileCallback callback) override {}

  std::unordered_map<uint32_t, zx::profile> profiles_by_priority_;
  fidl::BindingSet<fuchsia::scheduler::ProfileProvider> bindings_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_PROFILE_PROVIDER_H_
