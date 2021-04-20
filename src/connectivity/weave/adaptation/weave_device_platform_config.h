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

// Enable Thread Telemetry.
#define WEAVE_DEVICE_CONFIG_ENABLE_THREAD_TELEMETRY 1

// Enable Tunnel Telemetry.
#define WEAVE_DEVICE_CONFIG_ENABLE_TUNNEL_TELEMETRY 1

// The default size of individual debug event buffer is 256 bytes. Using the
// default size causes openweave to die, as openweave can't reserve enough space
// for some of the debug events which have a base size of 196 bytes and a
// few more bytes used for other headers which exceed 256.
// So increase the size of debug event buffer.
#define WEAVE_DEVICE_CONFIG_EVENT_LOGGING_DEBUG_BUFFER_SIZE 512

// Disable Weave-based time sync services, as these are handled in Fuchsia by
// other component(s).
#define WEAVE_DEVICE_CONFIG_ENABLE_WEAVE_TIME_SERVICE_TIME_SYNC 0
#define WEAVE_DEVICE_CONFIG_ENABLE_SERVICE_DIRECTORY_TIME_SYNC 0

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_DEVICE_PLATFORM_CONFIG_H_
