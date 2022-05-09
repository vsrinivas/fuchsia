// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/Profiles/data-management/Current/WdmManagedNamespace.h>
// clang-format on

// ==================== DataManagement Default Functions ====================

namespace nl::Weave::Profiles::DataManagement_Current::Platform {
// Only used in unit tests, the critical section is empty.
NL_DLL_EXPORT
void CriticalSectionEnter() {}
NL_DLL_EXPORT
void CriticalSectionExit() {}
}  // namespace nl::Weave::Profiles::DataManagement_Current::Platform
