// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

namespace {

class ProfileProvider : public fuchsia::media::ProfileProvider {
 public:
  ProfileProvider() = default;

  fidl::InterfaceRequestHandler<fuchsia::media::ProfileProvider> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // fuchsia::media::ProfileProvider implementation
  void RegisterHandlerWithCapacity(zx::thread thread_handle, std::string name, int64_t period,
                                   float capacity,
                                   RegisterHandlerWithCapacityCallback callback) override {
    // Does not actually set a thread profile, but provides a successful response to the request.
    callback(period, static_cast<int64_t>(static_cast<float>(period) * capacity));
  }

 private:
  fidl::BindingSet<fuchsia::media::ProfileProvider> bindings_;
};

}  // namespace

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto component_context = sys::ComponentContext::Create();

  ProfileProvider provider;
  component_context->outgoing()->AddPublicService(provider.GetHandler());
  component_context->outgoing()->ServeFromStartupInfo();

  loop.Run();
  return 0;
}
