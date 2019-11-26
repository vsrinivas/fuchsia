// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_TESTING_COMPONENT_BASE_H_
#define SRC_MODULAR_LIB_TESTING_COMPONENT_BASE_H_

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/modular/lib/fidl/single_service_app.h"
#include "src/modular/lib/testing/component_main.h"
#include "src/modular/public/src/modular/lib/integration_testing/cpp/reporting.h"
#include "src/modular/public/src/modular/lib/integration_testing/cpp/testing.h"

namespace modular_testing {

// A base class for components used in tests. It helps them to exit the
// application at the end of the life cycle while properly posting test points
// and calling TestRunner::Done().
//
// Component is fuchsia::modular::Module, fuchsia::modular::Agent,
// fuchsia::modular::SessionShell, etc.
template <typename Component>
class ComponentBase : protected SingleServiceApp<Component> {
 public:
  void Terminate(fit::function<void()> done) override { modular_testing::Done(std::move(done)); }

 protected:
  // Invocations of methods of the base class must be unambiguously recognizable
  // by the compiler as method invocations at the point of template definition,
  // because the base class depends on the template parameter. This can be
  // accomplished either by class name prefix or by prepending this->. Without
  // either, the calls are assumed to be function calls that are unresolved (and
  // marked as error by the compiler) at template definition. Essentially, the
  // class name prefix turns the independent name into a dependent
  // name. Cf. http://en.cppreference.com/w/cpp/language/dependent_name.
  using Base = SingleServiceApp<Component>;

  ComponentBase(sys::ComponentContext* const component_context)
      : Base(component_context), weak_factory_(this) {}

  ~ComponentBase() override = default;

  // We must not call modular_modular_testing::Init() in the base class
  // constructor, because that's before the test points are initialized. It's
  // fine to call this from the derived class constructor.
  void TestInit(const char* const file) { modular_testing::Init(Base::component_context(), file); }

  // Wraps the callback function into a layer that protects executing the
  // callback in the argument against execution after this instance is deleted,
  // using the weak pointer factory.
  fit::function<void()> Protect(fit::function<void()> callback) {
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

template <>
class ComponentBase<void> : protected ViewApp {
 public:
  void Terminate(fit::function<void()> done) override { modular_testing::Done(std::move(done)); }

 protected:
  ComponentBase(sys::ComponentContext* const component_context)
      : ViewApp(component_context), weak_factory_(this) {}

  ~ComponentBase() override = default;

  // We must not call modular_testing::Init() in the base class
  // constructor, because that's before the test points are initialized. It's
  // fine to call this from the derived class constructor.
  void TestInit(const char* const file) {
    modular_testing::Init(ViewApp::component_context(), file);
  }

  // Wraps the callback function into a layer that protects executing the
  // callback in the argument against execution after this instance is deleted,
  // using the weak pointer factory.
  fit::function<void()> Protect(fit::function<void()> callback) {
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

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_TESTING_COMPONENT_BASE_H_
