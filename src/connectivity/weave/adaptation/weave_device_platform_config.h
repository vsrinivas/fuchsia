// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_DEVICE_PLATFORM_CONFIG_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_DEVICE_PLATFORM_CONFIG_H_

// Disable the Weave Software Update Manager.
#define WEAVE_DEVICE_CONFIG_ENABLE_SOFTWARE_UPDATE_MANAGER 0

// Enable support for Thread in the Device Layer.
#define WEAVE_DEVICE_CONFIG_ENABLE_THREAD 1

// Enable Trait Manager.
#define WEAVE_DEVICE_CONFIG_ENABLE_TRAIT_MANAGER 1

// Disable Weave-based time sync service, as this is handled in Fuchsia by
// other component(s).
#define WEAVE_DEVICE_CONFIG_ENABLE_WEAVE_TIME_SERVICE_TIME_SYNC 0

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_DEVICE_PLATFORM_CONFIG_H_
