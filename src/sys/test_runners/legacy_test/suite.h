// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TEST_RUNNERS_LEGACY_TEST_SUITE_H_
#define SRC_SYS_TEST_RUNNERS_LEGACY_TEST_SUITE_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/test/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>

#include <map>
#include <memory>
#include <vector>

#include <garnet/bin/run_test_component/component.h>
#include <garnet/bin/run_test_component/test_metadata.h>

/// Implement and expose Suite protocol on behalf of wrapped legacy test component.
class Suite final : public fuchsia::test::Suite {
  using ComponentMap = std::map<run::Component*, std::unique_ptr<run::Component>>;

 public:
  Suite(std::shared_ptr<sys::ServiceDirectory> parent_env_svc,
        fuchsia::sys::EnvironmentPtr parent_env, std::shared_ptr<run::TestMetadata> test_metadata,
        std::shared_ptr<sys::ServiceDirectory> test_component_svc, std::string legacy_url,
        async_dispatcher_t* dispatcher);
  ~Suite() override;

  void GetTests(fidl::InterfaceRequest<fuchsia::test::CaseIterator> iterator) override;

  void Run(std::vector<fuchsia::test::Invocation> tests, fuchsia::test::RunOptions options,
           fidl::InterfaceHandle<fuchsia::test::RunListener> listener) override;

  fidl::InterfaceRequestHandler<fuchsia::test::Suite> GetHandler() {
    return bindings_.GetHandler(this, dispatcher_);
  }

  void AddBinding(zx::channel request) {
    bindings_.AddBinding(this, fidl::InterfaceRequest<fuchsia::test::Suite>(std::move(request)),
                         dispatcher_);
  }

 private:
  fpromise::promise<> RunTest(zx::socket out, zx::socket err,
                              const std::vector<std::string>& arguments,
                              fidl::InterfacePtr<fuchsia::test::CaseListener> case_listener);

  class CaseIterator final : public fuchsia::test::CaseIterator {
   public:
    CaseIterator(fidl::InterfaceRequest<fuchsia::test::CaseIterator> request,
                 async_dispatcher_t* dispatcher, fit::function<void(CaseIterator*)> done_callback);

    void GetNext(GetNextCallback callback) override;

   private:
    int get_next_call_count = 0;
    fidl::Binding<fuchsia::test::CaseIterator> binding_;
    fit::function<void(CaseIterator*)> done_callback_;
  };

  const fuchsia::sys::EnvironmentPtr parent_env_;
  const std::shared_ptr<sys::ServiceDirectory> parent_env_svc_;
  const std::shared_ptr<sys::ServiceDirectory> test_component_svc_;
  const std::shared_ptr<run::TestMetadata> test_metadata_;
  const std::string legacy_url_;
  std::shared_ptr<ComponentMap> test_components_;
  std::map<CaseIterator*, std::unique_ptr<CaseIterator>> case_iterators_;
  async_dispatcher_t* dispatcher_;
  fidl::BindingSet<fuchsia::test::Suite> bindings_;
  async::Executor executor_;

  std::unique_ptr<CaseIterator> RemoveCaseInterator(CaseIterator*);
  std::unique_ptr<run::Component> RemoveComponent(run::Component* ptr);
};

#endif  // SRC_SYS_TEST_RUNNERS_LEGACY_TEST_SUITE_H_
