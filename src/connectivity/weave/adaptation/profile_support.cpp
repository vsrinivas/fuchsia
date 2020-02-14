// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/Profiles/data-management/Current/WdmManagedNamespace.h>
// clang-format on

// ==================== DataManagement Default Functions ====================

namespace nl {
namespace Weave {
namespace Profiles {
namespace WeaveMakeManagedNamespaceIdentifier(DataManagement,
                                              kWeaveManagedNamespaceDesignation_Current) {
  namespace Platform {
  // for unit tests, the dummy critical section is sufficient.
  void CriticalSectionEnter() { return; }

  void CriticalSectionExit() { return; }

  }  // namespace Platform
}  // namespace )
}  // namespace Profiles
}  // namespace Weave
}  // namespace nl
