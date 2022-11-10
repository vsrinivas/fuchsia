// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_COMPONENT_INCOMING_CPP_CONSTANTS_H_
#define LIB_COMPONENT_INCOMING_CPP_CONSTANTS_H_

namespace component {

// The name of the default FIDL Service instance.
constexpr const char kDefaultInstance[] = "default";

// The path referencing the incoming services directory.
constexpr const char kServiceDirectory[] = "/svc";

// The path prefix referencing the incoming services directory,
// with a trailing slash.
constexpr const char kServiceDirectoryTrailingSlash[] = "/svc/";

// The path referencing the incoming services directory.
constexpr const char kServiceDirectoryWithNoSlash[] = "svc";

}  // namespace component

#endif  // LIB_COMPONENT_INCOMING_CPP_CONSTANTS_H_
