// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_DEVICE_INFO_DEVICE_INFO_H_
#define PERIDOT_LIB_DEVICE_INFO_DEVICE_INFO_H_

#include <string>

namespace modular {

// Reads the contents of /data/device/profile_config.json and returns the
// result. If the file cannot be read, returns empty JSON-encoded object, "{}".
std::string LoadDeviceProfile();

// Reads a device id, scoped to |user|, for this device. Generates and persists
// one on the first call of LoadDeviceID() on this device for this |user|.
//
// Thread-hostile.
std::string LoadDeviceID(const std::string& user);

// A wrapper around gethostname() which a) translates an error into the string
// "fuchsia" and b) caches the output to a file scoped by |user| for future
// calls.
//
// Thread-hostile.
std::string LoadDeviceName(const std::string& user);

}  // namespace modular

#endif  // PERIDOT_LIB_DEVICE_INFO_DEVICE_INFO_H_
