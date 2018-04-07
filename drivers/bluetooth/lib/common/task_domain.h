// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/type_support.h>

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/threading/create_thread.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

#include "garnet/drivers/bluetooth/lib/common/create_thread.h"

namespace btlib {
namespace common {

// A task domain is a mixin for objects that maintain state that needs to be
// accessed exclusively on a specific thread's TaskRunner.
//
//   * A TaskDomain can be initialized with a task runner representing the
//     serialization domain. If not, TaskDomain will spawn a thread with the
//     runner.
//
//   * TaskDomain provides a PostMessage() method which can be used to schedule
//     a task on the domain. The TaskDomain is guaranteed to remain alive during
//     the task execution. This guarantee requires that T derive from
//     fbl::RefCounted<T>.
//
//   * Tasks that are posted or run after clean up will be ignored. The
//     ScheduleCleanUp() method must be called before all references to T are
//     dropped.
//
//     T must provide a CleanUp() method, which will be scheduled on the
//     domain's task runner by ScheduleCleanUp(). This can be used to clean up
//     state that is restricted to the task runner.
//
// EXAMPLE:
//
//   class MyObject : public fbl::RefCounted<MyObject>,
//                    public TaskDomain<MyObject> {
//    public:
//     // Initialize spawning a thread.
//     MyObject() : common::TaskDomain<MyObject>("my-thread") {}
//
//     // Initialize to run on |task_runner|.
//     MyObject(fxl::RefPtr<fxl::TaskRunner> task_runner)
//        : common::TaskDomain<MyObject>(task_runner) {}
//
//     void CleanUp()
//
//    private:
//     int foo_;
//   };
//
// If MyObject indirectly inherits from fbl::RefCounted, the base type needs to
// be supplied using an additional template parameter:
//
//   class MyBase : public fbl::RefCounted<MyBase> {};
//
//   class MyObject : public MyBase, public TaskDomain<MyObject, MyBase> {
//    public:
//     ...
//   };
//
// If CleanUp() should be private:
//
//   class MyObject : public fbl::RefCounted<MyObject>,
//                    public TaskDomain<MyObject> {
//    public:
//     ...
//    private:
//     BT_FRIEND_TASK_DOMAIN(MyObject);
//
//     void CleanUp() { ... }
//   };
//

namespace internal {
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_clean_up, CleanUp, void (T::*)(void));
}  // namespace internal

// TODO(NET-695): Remove all references to fsl::MessageLoop and fxl::TaskRunner
// once nothing depends on them.
template <typename T, typename RefCountedType = T>
class TaskDomain {
 protected:
  // Initializes this domain by spawning a new thread with a TaskRunner.
  // |name| is assigned to the thread.
  TaskDomain(T* obj, std::string name) : owns_thread_(true) {
    Init(obj);

    std::thread thrd =
        common::CreateThread(&task_runner_, &dispatcher_, std::move(name));
    FXL_DCHECK(task_runner_);
    FXL_DCHECK(dispatcher_);
    thrd.detach();
  }

  // Initializes this domain by assigning the given |task_runner| to it.
  // TODO(armansito): For now this needs both a TaskRunner and async_t so that
  // the dependency on TaskRunner can be removed in pieces.
  TaskDomain(T* obj,
             fxl::RefPtr<fxl::TaskRunner> task_runner,
             async_t* dispatcher)
      : owns_thread_(false),
        task_runner_(task_runner),
        dispatcher_(dispatcher) {
    Init(obj);
    FXL_DCHECK(task_runner_);
    FXL_DCHECK(dispatcher_);
  }

  virtual ~TaskDomain() {
    FXL_DCHECK(!alive_)
        << "ScheduleCleanUp() must be called before destruction";
  }

  // Runs the object's CleanUp() handler on the domain's TaskRunner. Quits the
  // event loop if the domain owns its thread.
  void ScheduleCleanUp() {
    PostMessage([obj = obj_, this] {
      alive_ = false;
      obj->CleanUp();

      // Quit the thread if it's owned by this object.
      if (owns_thread_) {
        fsl::MessageLoop::GetCurrent()->QuitNow();
      }
    });
  }

  fxl::RefPtr<fxl::TaskRunner> task_runner() const { return task_runner_; }
  async_t* dispatcher() const { return dispatcher_; }

  void PostMessage(fbl::Function<void()> func) {
    // |objref| is captured here to make sure |obj_| stays alive until |func|
    // has run.
    async::PostTask(dispatcher_, [this, func = std::move(func),
                                  objref = fbl::WrapRefPtr(obj_)] {
      if (alive_) {
        func();
      }
    });
  }

 private:
  void Init(T* obj) {
    FXL_DCHECK(obj);
    obj_ = obj;
    alive_ = true;

    static_assert(std::is_base_of<fbl::RefCounted<RefCountedType>, T>::value,
                  "T must support fbl::RefPtr");
    static_assert(std::is_base_of<TaskDomain<T, RefCountedType>, T>::value,
                  "TaskDomain can only be used as a mixin");
    static_assert(internal::has_clean_up<T>::value,
                  "T must provide a CleanUp() method");
  }

  T* obj_;
  std::atomic_bool alive_;
  bool owns_thread_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  async_t* dispatcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TaskDomain);
};

#define BT_FRIEND_TASK_DOMAIN(Type) BT_FRIEND_TASK_DOMAIN_FULL(Type, Type)
#define BT_FRIEND_TASK_DOMAIN_FULL(Type, RefCountedType) \
    friend class ::btlib::common::TaskDomain<Type, RefCountedType>; \
    friend struct ::btlib::common::internal::has_clean_up<Type>

}  // namespace common
}  // namespace btlib
