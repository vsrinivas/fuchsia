// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_DEVICE_ID_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_DEVICE_ID_H_

#include <string>

namespace feedback {

// Creates a new device id and stores it at |path| if the file doesn't exist or contains an
// invalid id.
//
// The id is a 128-bit (pseudo) random UUID in the form of version 4 as described in RFC 4122,
// section 4.4.
bool InitializeDeviceId(const std::string& path);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_DEVICE_ID_H_
