// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_SERVICE_CONNECTOR_H_
#define LIB_FIDL_CPP_SERVICE_CONNECTOR_H_

namespace fidl {

// A connector for service instances.
class ServiceConnector {
 public:
  virtual ~ServiceConnector() = default;

  // Connect to a |path| within a serviec instance, using |channel|.
  virtual zx_status_t Connect(const std::string& path, zx::channel channel) const = 0;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_SERVICE_CONNECTOR_H_
