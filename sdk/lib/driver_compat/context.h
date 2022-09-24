// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_COMPAT_CONTEXT_H_
#define LIB_DRIVER_COMPAT_CONTEXT_H_

#include <fidl/fuchsia.driver.compat/cpp/fidl.h>
#include <lib/driver2/devfs_exporter.h>
#include <lib/driver2/driver_context.h>

namespace compat {

// This class holds the compat contextual information that a driver cares about, like
// the topological path and devfs exporter.
class Context {
 public:
  // Create a Context. The pointers to |driver_context| and |dispatcher| are unowned and must
  // outlive this class.
  // This function attempts to connect to `/svc/fuchsia.driver.compat.Service/default/device` as
  // well as `/svc/fuchsia.device.fs.Exporter`.
  static void ConnectAndCreate(driver::DriverContext* driver_context,
                               async_dispatcher_t* dispatcher,
                               fit::callback<void(zx::status<std::unique_ptr<Context>>)> callback);

  // Given a |relative_child_path| return that child's full topological path.
  std::string TopologicalPath(std::string_view relative_child_path) const;

  const driver::DevfsExporter& devfs_exporter() const { return devfs_exporter_; }

 private:
  std::string parent_topological_path_;
  fidl::SharedClient<fuchsia_driver_compat::Device> parent_device_;
  driver::DevfsExporter devfs_exporter_;
};

}  // namespace compat

#endif  // LIB_DRIVER_COMPAT_CONTEXT_H_
