// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_JOB_HOLDER_H_
#define GARNET_BIN_APPMGR_JOB_HOLDER_H_

#include <fs/managed-vfs.h>
#include <zx/channel.h>

#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>

#include "garnet/bin/appmgr/application_controller_impl.h"
#include "garnet/bin/appmgr/application_environment_controller_impl.h"
#include "garnet/bin/appmgr/application_namespace.h"
#include "garnet/bin/appmgr/application_runner_holder.h"
#include <fuchsia/cpp/component.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace component {

class JobHolder {
 public:
  JobHolder(JobHolder* parent,
            zx::channel host_directory,
            fidl::StringPtr label);
  ~JobHolder();

  JobHolder* parent() const { return parent_; }
  const std::string& label() const { return label_; }

  const fbl::RefPtr<fs::PseudoDir>& info_dir() const { return info_dir_; }

  void CreateNestedJob(
      zx::channel host_directory,
      fidl::InterfaceRequest<ApplicationEnvironment> environment,
      fidl::InterfaceRequest<ApplicationEnvironmentController> controller,
      fidl::StringPtr label);

  void CreateApplication(
      ApplicationLaunchInfo launch_info,
      fidl::InterfaceRequest<ApplicationController> controller);

  // Removes the child job holder from this job holder and returns the owning
  // reference to the child's controller. The caller of this function typically
  // destroys the controller (and hence the environment) shortly after calling
  // this function.
  std::unique_ptr<ApplicationEnvironmentControllerImpl> ExtractChild(
      JobHolder* child);

  // Removes the application from this environment and returns the owning
  // reference to the application's controller. The caller of this function
  // typically destroys the controller (and hence the application) shortly after
  // calling this function.
  std::unique_ptr<ApplicationControllerImpl> ExtractApplication(
      ApplicationControllerImpl* controller);

  void AddBinding(fidl::InterfaceRequest<ApplicationEnvironment> environment);

 private:
  static uint32_t next_numbered_label_;

  ApplicationRunnerHolder* GetOrCreateRunner(const std::string& runner);

  void CreateApplicationWithProcess(
      ApplicationPackagePtr package,
      ApplicationLaunchInfo launch_info,
      fidl::InterfaceRequest<ApplicationController> controller,
      fxl::RefPtr<ApplicationNamespace> application_namespace);
  void CreateApplicationFromPackage(
      ApplicationPackagePtr package,
      ApplicationLaunchInfo launch_info,
      fidl::InterfaceRequest<ApplicationController> controller,
      fxl::RefPtr<ApplicationNamespace> application_namespace);

  zx::channel OpenRootInfoDir();

  JobHolder* const parent_;
  ApplicationLoaderPtr loader_;
  std::string label_;

  zx::job job_;
  zx::job job_for_child_;

  fxl::RefPtr<ApplicationNamespace> default_namespace_;

  // A pseudo-directory which describes the components within the scope of
  // this job.
  fbl::RefPtr<fs::PseudoDir> info_dir_;
  fs::ManagedVfs info_vfs_;

  std::unordered_map<JobHolder*,
                     std::unique_ptr<ApplicationEnvironmentControllerImpl>>
      children_;

  std::unordered_map<ApplicationControllerImpl*,
                     std::unique_ptr<ApplicationControllerImpl>>
      applications_;

  std::unordered_map<std::string, std::unique_ptr<ApplicationRunnerHolder>>
      runners_;

  FXL_DISALLOW_COPY_AND_ASSIGN(JobHolder);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_JOB_HOLDER_H_
