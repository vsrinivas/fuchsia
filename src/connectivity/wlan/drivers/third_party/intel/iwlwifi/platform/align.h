// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_ALIGN_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_ALIGN_H_

// Created to avoid dependency on lib/zircon-internal/align.h, since this will not
// be available OOT.
#define IWL_ROUNDUP(a, b)       \
  ({                            \
    const __typeof(a) _a = (a); \
    const __typeof(b) _b = (b); \
    ((_a + _b - 1) / _b * _b);  \
  })

#define IWL_ALIGN(a, b) IWL_ROUNDUP(a, b)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_ALIGN_H_
