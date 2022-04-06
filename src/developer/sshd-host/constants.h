// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SSHD_HOST_CONSTANTS_H_
#define SRC_DEVELOPER_SSHD_HOST_CONSTANTS_H_

#include <string_view>

namespace sshd_host {

inline constexpr std::string_view kAuthorizedKeyPathInData = "ssh/authorized_keys";
inline constexpr std::string_view kAuthorizedKeysBootloaderFileName = "ssh.authorized_keys";

}  // namespace sshd_host

#endif  // SRC_DEVELOPER_SSHD_HOST_CONSTANTS_H_
