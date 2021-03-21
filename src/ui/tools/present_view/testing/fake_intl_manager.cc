// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tools/present_view/testing/fake_intl_manager.h"

#include <functional>

namespace present_view::testing {

FakeIntlManager::FakeIntlManager(
    fuchsia::sys::StartupInfo startup_info,
    std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component)
    : component_context_(sys::ServiceDirectory::CreateFromNamespace(),
                         std::move(startup_info.launch_info.directory_request)),
      intercepted_component_(std::move(intercepted_component)) {}

FakeIntlManager::~FakeIntlManager() {}

}  // namespace present_view::testing
