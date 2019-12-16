// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_CALLBACK_SCOPED_TASK_RUNNER_H_
#define SRC_LEDGER_LIB_CALLBACK_SCOPED_TASK_RUNNER_H_

#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/fit/function_traits.h>
#include <lib/zx/time.h>

#include <memory>

namespace ledger {

class TaskController {
 public:
  template <class T>
  struct Tag {};

  TaskController();
  TaskController(const TaskController&) = delete;
  TaskController& operator=(const TaskController&) = delete;
  virtual ~TaskController();

  // Indicates that further calls to |RunTask| should no-op. Implementations may
  // choose to block until running or outstanding tasks have completed.
  //
  // This method must be idempotent.
  virtual void ShutDown() = 0;
  // Runs a task immediately, or no-ops if this controller has shut down.
  virtual void RunTask(fit::closure task) = 0;
};

// A basic task controller that does not synchronize shutdown with task
// execution. Client code is responsible for ensuring that any tasks that may be
// running when the runner is destroyed do not rely on invalid state, typically
// by ensuring that |ShutDown| occurs on the dispatch thread.
class SimpleTaskController : public TaskController {
 public:
  // For ergonomics, this could be a constexpr static Tag<SimpleTaskController>
  // but C++ would then allocate a (small) nonzero size for it. If we only use
  // it inline, it can be optimized out.
  // http://www.open-std.org/jtc1/sc22/open/n2356/class.html, s9p3
  using Type = Tag<SimpleTaskController>;

 private:
  // |TaskController|
  // This implementation does not block, and should be called on the dispatch
  // thread.
  void ShutDown() override;
  // |TaskController|
  void RunTask(fit::closure task) override;

  // This could easily be a std::atomic_bool, but since this implementation is
  // not synchronized, in correct code I don't think there's a case where this
  // makes a difference.
  //
  // A common idiom is to use |fxl::WeakPtr| for this kind of guard. However,
  // that is not thread-safe as |fxl::WeakPtr| contains non-atomic flag
  // twiddling, and |fxl::WeakPtr::InvalidateWeakPtrs| allows new valid pointers
  // to be created after it is called so it cannot be used as a |ShutDown|
  // method. Despite being unsynchronized, |SimpleTaskController| lets the
  // caller |ShutDown| on the dispatch thread.
  bool alive_ = true;
};

// An object that wraps the posting logic of an |async_t|, but that is
// not copyable and will generally not run any task after being deleted, though
// edge case handling may differ between |Controller| implementations.
//
// This class is mostly thread-safe, though it must not go out of scope while
// any of its methods are being called, and handling of edge cases varies
// depending on the controller implementation. Notably, the default |Controller|
// is not synchronized, so |ShutDown| should occur on the dispatch thread (after
// which destruction may happen on any thread).
//
// Typically, this class should appear towards the end of the fields of an
// owning class so that it goes out of scope before any state that tasks may
// depend on.
//
// This class may be used without a dispatcher, but the common use case is to
// manage FIDL calls.
class ScopedTaskRunner {
 public:
  explicit ScopedTaskRunner(async_dispatcher_t* dispatcher = async_get_default_dispatcher());

  template <class Controller>
  explicit ScopedTaskRunner(typename TaskController::Tag<Controller> controller_type,
                            async_dispatcher_t* dispatcher = async_get_default_dispatcher())
      : dispatcher_(dispatcher), controller_(std::make_shared<Controller>()) {}

  ScopedTaskRunner(const ScopedTaskRunner&) = delete;
  ScopedTaskRunner(ScopedTaskRunner&&);

  ~ScopedTaskRunner();

  ScopedTaskRunner& operator=(const ScopedTaskRunner&) = delete;
  ScopedTaskRunner& operator=(ScopedTaskRunner&&);

  async_dispatcher_t* dispatcher() const { return dispatcher_; }

  // Pre-emptively ends this ScopedTaskRunner's lifecycle. All subsequent tasks
  // will no-op. This method may block, depending on the |TaskController|
  // implementation. The default implementation does not block, so care should
  // be taken by callers that any tasks executing during shutdown do not depend
  // on state guarded by this instance, typically by calling this method from
  // the dispatch thread.
  //
  // This method is idempotent and will automatically be called when this class
  // goes out of scope.
  void ShutDown();

  // Shuts down the current controller and assigns a new |SimpleTaskController|.
  void Reset();

  // Shuts down the current controller and assigns a new one of the specified type.
  template <class Controller>
  void Reset(typename TaskController::Tag<Controller> controller_type) {
    ShutDown();
    controller_ = std::make_shared<Controller>();
  }

  // Posts a task to run as soon as possible on the dispatcher after the current
  // dispatch cycle.
  void PostTask(fit::closure task);

  // Posts a task to run as soon as possible after the specified |target_time|.
  void PostTaskForTime(fit::closure task, zx::time target_time);

  // Posts a task to run as soon as possible after the specified |delay|.
  void PostDelayedTask(fit::closure task, zx::duration delay);

  // Convenience function to post a repeating periodic task. If |invoke_now| is
  // true, the task is run as soon as possible on the dispatcher after the
  // current dispatch cycle as well as periodically. Otherwise, the first
  // invocation of the task will be as soon as possible after the specified
  // |interval|.
  void PostPeriodicTask(fit::closure task, zx::duration interval, bool invoke_now = true);

  // Scopes a task to the current task runner without scheduling it. This means
  // that the given function will be called when the returned function is called
  // if and only if this task runner has not been deleted or shut down.
  // Synchronization of the guard depends on the |TaskController|
  // implementation; the default implementation is unsynchronized.
  template <class Task, class... Args>
  auto MakeScoped(Task task, fit::parameter_pack<Args...>) {
    return [controller = controller_, task = std::move(task)](Args... args) mutable {
      // This differs from |MakeScoped| in that |controller| is aware
      // of the task from start to finish, as opposed to |MakeScoped|
      // which only consults |witness| when the task begins.
      return controller->RunTask(
          [task = std::move(task), &args...]() mutable { task(std::forward<Args>(args)...); });
    };
  }

  template <class Task>
  auto MakeScoped(Task task) {
    return MakeScoped(std::move(task), typename fit::function_traits<Task>::args{});
  }

 private:
  async_dispatcher_t* dispatcher_;
  std::shared_ptr<TaskController> controller_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_CALLBACK_SCOPED_TASK_RUNNER_H_
