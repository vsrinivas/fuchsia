// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FUCHSIA_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FUCHSIA_DEVICE_H_

// This file contains device DDK code that operates as a compatibility layer between the Linux and
// Fuchsia driver models.

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// This struct is analogous to the Linux device struct, and contains all the Fuchsia-specific data
// fields relevant to generic device functionality.
struct device {
  // The zx_device of the base iwlwifi Device.
  struct zx_device* zxdev;

  // The dispatcher used to dispatch work queue tasks, equivalent to the Linux workqueue.  On Linux,
  // these are run in process context, in contrast with timer tasks that are run in interrupt
  // context.  Fuchsia drivers have no separate interrupt context, but to maintain similar
  // performance characteristics we will maintain a dedicated work queue dispatcher here.
  struct async_dispatcher* task_dispatcher;
};

// Release a driver instance.
void iwl_device_release(struct device* device);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FUCHSIA_DEVICE_H_
