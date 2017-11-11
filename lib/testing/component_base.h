// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_COMPONENT_BASE_H_
#define PERIDOT_LIB_TESTING_COMPONENT_BASE_H_

#include "lib/app/cpp/connect.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

namespace modular {
namespace testing {

// A base class for components used in tests. It helps them to exit the
// application at the end of the life cycle while properly posting test points
// and calling TestRunner::Done().
//
// Component is modular::Module, modular::Agent, modular::UserShell, etc.
template <typename Component>
class ComponentBase : protected SingleServiceApp<Component> {
 public:
  void Terminate(std::function<void()> done) override {
    modular::testing::Done(done);
  }

 protected:
  // Invocations of methods of the base class must be unambiguously recognizable
  // by the compiler as method invocations at the point of template definition,
  // because the base class depends on the template parameter. This can be
  // accomplished either by class name prefix or by prepending this->. Without
  // either, the calls are assumed to be function calls that are unresolved (and
  // marked as error by the compiler) at template definition. Essentially, the
  // class name prefix turns the independent name into a dependent
  // name. Cf. http://en.cppreference.com/w/cpp/language/dependent_name.
  using Base = modular::SingleServiceApp<Component>;

  ComponentBase(app::ApplicationContext* const application_context) :
      Base(application_context), weak_factory_(this) {}

  ~ComponentBase() override = default;

  void TestInit(const char* const file) {
    // We must not call testing::Init() in the base class constructor, because
    // that's before the test points are initialized. It's fine to call this
    // form the derived class constructor.
    modular::testing::Init(Base::application_context(), file);
  }

  // Wraps the callback function into a layer that protects executing the
  // callback in the argument against execution after this instance is deleted,
  // using the weak pointer factory.
  std::function<void()> Protect(std::function<void()> callback) {
    return [ptr = weak_factory_.GetWeakPtr(), callback = std::move(callback)] {
      if (ptr) {
        callback();
      }
    };
  }

 private:
  // This weak ptr factory is not the last member in the derived class, so it
  // cannot be used to protect code executed in member destructors against
  // accessing this. But it is enough to protect callbacks sent to the runloop
  // against execution after the instance is deleted.
  fxl::WeakPtrFactory<ComponentBase> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentBase);
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_COMPONENT_BASE_H_
