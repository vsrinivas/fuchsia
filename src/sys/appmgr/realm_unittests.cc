// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/namespace.h>
#include <lib/sys/cpp/testing/test_with_environment_fixture.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <zircon/process.h>
#include <zircon/rights.h>
#include <zircon/syscalls/object.h>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/sys/appmgr/realm.h"

using fuchsia::sys::ServiceList;
using fuchsia::sys::ServiceListPtr;

namespace component {
namespace {

class NamespaceGuard {
 public:
  explicit NamespaceGuard(fxl::RefPtr<Namespace> ns) : ns_(std::move(ns)) {}
  NamespaceGuard(std::nullptr_t) : ns_(nullptr) {}
  ~NamespaceGuard() { Kill(); }
  Namespace* operator->() const { return ns_.get(); }

  fxl::RefPtr<Namespace>& ns() { return ns_; }

  void Kill() {
    if (ns_) {
      ns_->FlushAndShutdown(ns_);
    }
    ns_ = nullptr;
  }

 private:
  fxl::RefPtr<Namespace> ns_;
};

class RealmTest : public gtest::TestWithEnvironmentFixture {
 protected:
  RealmTest() {}
  NamespaceGuard MakeNamespace(ServiceListPtr additional_services,
                               NamespaceGuard parent = nullptr) {
    if (parent.ns().get() == nullptr) {
      return NamespaceGuard(
          fxl::MakeRefCounted<Namespace>(nullptr, std::move(additional_services), nullptr));
    }
    return NamespaceGuard(Namespace::CreateChildNamespace(parent.ns(), nullptr,
                                                          std::move(additional_services), nullptr));
  }

  zx_status_t AddService(const std::string& name) {
    auto cb = [this, name](zx::channel channel, async_dispatcher_t* dispatcher) {
      ++connection_ctr_[name];
    };
    return directory_.AddEntry(name, std::make_unique<vfs::Service>(cb));
  }

  vfs::PseudoDir directory_;
  std::map<std::string, int> connection_ctr_;
};

// This test checks that if the process can not be created that structures are cleaned up.
TEST_F(RealmTest, ProcessCreationFailure) {
  // Create a namespace to be used for the component.
  ServiceListPtr service_list(new ServiceList);
  static constexpr char kService1[] = "fuchsia.test.TestService1";
  static constexpr char kService2[] = "fuchsia.test.TestService2";
  service_list->names.push_back(kService1);
  service_list->names.push_back(kService2);
  AddService(kService1);
  AddService(kService2);

  auto ns = MakeNamespace(std::move(service_list));
  fdio_ns_t* fdio_ns;
  fdio_ns_create(&fdio_ns);

  zx::channel ch0, ch1;
  auto status = zx::channel::create(0, &ch0, &ch1);
  ASSERT_EQ(status, ZX_OK);

  static constexpr char kNSService1[] = "/svc/fuchsia.ns.TestService";
  fdio_ns_bind(fdio_ns, kNSService1, ch1.get());

  fdio_flat_namespace_t* flat_ns;
  fdio_ns_export(fdio_ns, &flat_ns);

  zx_handle_t job = zx_job_default();
  zx_handle_t child;
  status = zx_job_create(job, 0, &child);
  ASSERT_EQ(status, ZX_OK);

  zx_handle_t child_dupe;
  status = zx_handle_duplicate(child, ZX_RIGHT_SAME_RIGHTS, &child_dupe);
  ASSERT_EQ(status, ZX_OK);

  zx::job child_job = zx::job(child);

  // Create a child process object, but don't actually create a process.
  zx::process child_process = zx::process();
  ASSERT_FALSE(child_process);

  // Pass in some placeholder values, since the process is invalidd, nothing
  // here will actually be used.
  Realm* no_realm = nullptr;
  std::string args = "";
  fuchsia::sys::ComponentControllerPtr component_controller;
  ComponentRequestWrapper component_req =
      ComponentRequestWrapper(component_controller.NewRequest());
  std::string url = "";
  ExportedDirChannels channels = ExportedDirChannels();
  fit::function<void(std::weak_ptr<ComponentControllerImpl> component)> callback =
      [](std::weak_ptr<ComponentControllerImpl> component) {};
  zx::channel pkg_hnd;

  ASSERT_EQ(ns.ns()->status(), Namespace::Status::RUNNING);

  zx_info_job_t job_info;
  size_t info_actual, info_available;
  status = zx_object_get_info(child_dupe, ZX_INFO_JOB, &job_info, sizeof(zx_info_job_t),
                              &info_actual, &info_available);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(false, job_info.exited);

  // Execute an immediate wait on all the flat namespace handles to check they
  // are valid.
  for (size_t i = 0; i < flat_ns->count; i++) {
    zx_signals_t observed;
    status = zx_object_wait_one(flat_ns->handle[i], ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE_PAST,
                                &observed);
    ASSERT_NE(ZX_ERR_BAD_HANDLE, status);
  }

  Realm::InstallRuntime(no_realm, std::move(child_job), std::move(child_process), ns.ns(), flat_ns,
                        args, std::move(component_req), url, std::move(channels),
                        std::move(callback), std::move(pkg_hnd));

  // Check that all the things we expect to be torn down are torn down.

  // The job should have exited.
  status = zx_object_get_info(child_dupe, ZX_INFO_JOB, &job_info, sizeof(zx_info_job_t),
                              &info_actual, &info_available);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(true, job_info.exited);

  // Execute a wait that will return immediately and expect all theh handles to
  // be be invalid since they should have been closed.
  for (size_t i = 0; i < flat_ns->count; i++) {
    zx_signals_t observed;
    status = zx_object_wait_one(flat_ns->handle[i], ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE_PAST,
                                &observed);
    ASSERT_EQ(ZX_ERR_BAD_HANDLE, status);
  }

  // The namespace should not be marked running.
  ASSERT_NE(ns.ns()->status(), Namespace::Status::RUNNING);

  fdio_ns_free_flat_ns(flat_ns);
  fdio_ns_destroy(fdio_ns);
}

}  // namespace
}  // namespace component
