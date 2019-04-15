// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MCS_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MCS_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/element.h>

namespace wlan {

SupportedMcsSet IntersectMcs(
    const SupportedMcsSet& lhs,
    const ::fuchsia::wlan::mlme::SupportedMcsSet& fidl);
SupportedMcsSet IntersectMcs(const ::fuchsia::wlan::mlme::SupportedMcsSet& fidl,
                             const SupportedMcsSet& lhs);
SupportedMcsSet SupportedMcsSetFromFidl(
    const ::fuchsia::wlan::mlme::SupportedMcsSet& fidl);

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MCS_H_
