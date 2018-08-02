// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/element.h>

namespace wlan {

SupportedMcsSet IntersectMcs(const SupportedMcsSet& lhs, const SupportedMcsSet& rhs);
SupportedMcsSet IntersectMcs(const SupportedMcsSet& lhs,
                             const ::fuchsia::wlan::mlme::SupportedMcsSet& fidl);
SupportedMcsSet IntersectMcs(const ::fuchsia::wlan::mlme::SupportedMcsSet& fidl,
                             const SupportedMcsSet& lhs);
SupportedMcsSet SupportedMcsSetFromFidl(const ::fuchsia::wlan::mlme::SupportedMcsSet& fidl);

}  // namespace wlan
