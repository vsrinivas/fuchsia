// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_TESTING_COMPONENT_BASE_H_
#define APPS_MODULAR_LIB_TESTING_COMPONENT_BASE_H_

#include "lib/app/cpp/connect.h"
#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {
namespace testing {

// A base class for components used in tests. It helps them to implement a
// proper life cycle and to exit the application at the end of the life cycle
// while properly posting test points and calling TestRunner::Done().
//
// Requirements for using this base class:
//
// 1. The derived class instance MUST be created with new, because
//    DeleteAndQuit() calls delete this.
//
// 2. Callbacks posted to the run loop that invoke DeleteAndQuit() MUST invoke
//    it on a weak pointer obtained from GetWeakPtr(). This can be done by
//    wrapping them in a Protect() call (cf. below).
//
// Component is modular::Module, modular::Agent, modular::UserShell, etc.
template <typename Component>
class ComponentBase : protected SingleServiceApp<Component> {
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

  ComponentBase() : weak_factory_(this) {}

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
    return
        [ ptr = weak_factory_.GetWeakPtr(), callback = std::move(callback) ] {
      if (ptr) {
        callback();
      }
    };
  }

  // Delete alone is used to simulate the "unstoppable agent".
  void Delete(const std::function<void()>& done) {
    Base::PassBinding()->Close();
    modular::testing::Done([this, done] {
      delete this;
      done();
    });
  }

  // Used by non-Agents.
  // TODO(vardhan): Once all components convert to using |Lifecycle|,
  // don't PassBinding() here anymore, replace this with
  // DeleteAndQuitWithoutBinding.
  void DeleteAndQuit(const std::function<void()>& done = [] {}) {
    modular::testing::Done([this, done] {
      auto binding =
          Base::PassBinding();  // To invoke done() after delete this.
      delete this;
      done();
      mtl::MessageLoop::GetCurrent()->QuitNow();
    });
  }

  void DeleteAndQuitAndUnbind() {
    Base::PassBinding()->Close();
    modular::testing::Done([this] {
      delete this;
      mtl::MessageLoop::GetCurrent()->QuitNow();
    });
  }

 private:
  // This weak ptr factory is not the last member in the derived class, so it
  // cannot be used to protect code executed in member destructors against
  // accessing this. But it is enough to protect callbacks sent to the runloop
  // against execution after the instance is deleted.
  ftl::WeakPtrFactory<ComponentBase> weak_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ComponentBase);
};

}  // namespace testing
}  // namespace modular

#endif  // APPS_MODULAR_LIB_TESTING_COMPONENT_BASE_H_
