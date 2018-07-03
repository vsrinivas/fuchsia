// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_CPP_OPERATION_H_
#define LIB_ASYNC_CPP_OPERATION_H_

#include <functional>
#include <memory>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

#include <lib/async/cpp/future.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/memory/weak_ptr.h>

namespace modular {
class OperationBase;

// An abstract base class which provides methods to hold on to Operation
// instances until they declare themselves to be Done(). Ownership of
// the operations must be managed by implementations.
class OperationContainer {
 public:
  OperationContainer();
  virtual ~OperationContainer();

  // Adds |o| to this container and takes ownership.
  virtual void Add(OperationBase* o) final;

 protected:
  void Schedule(OperationBase* o);
  void InvalidateWeakPtrs(OperationBase* o);

 private:
  // OperationBase calls the methods below.
  friend class OperationBase;

  virtual fxl::WeakPtr<OperationContainer> GetWeakPtr() = 0;
  // Must take ownership of |o|.
  virtual void Hold(OperationBase* o) = 0;
  // Must clean up memory for |o|.
  virtual void Drop(OperationBase* o) = 0;
  virtual void Cont() = 0;
};

// An implementation of |OperationContainer| which runs every instance of
// Operation as soon as it arrives.
class OperationCollection : public OperationContainer {
 public:
  OperationCollection();
  ~OperationCollection() override;

 private:
  fxl::WeakPtr<OperationContainer> GetWeakPtr() override;
  void Hold(OperationBase* o) override;
  void Drop(OperationBase* o) override;
  void Cont() override;

  std::vector<std::unique_ptr<OperationBase>> operations_;

  // It is essential that the weak_ptr_factory is defined after the operations_
  // container, so that it gets destroyed before operations_, and hence before
  // the Operation instances in it, so that the Done() call on their
  // OperationBase sees the container weak pointer to be null. That's also why
  // we need the virtual GetWeakPtr() in the base class, rather than to put the
  // weak ptr factory in the base class.
  fxl::WeakPtrFactory<OperationContainer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(OperationCollection);
};

// An implementation of |OperationContainer| which runs incoming Operations
// sequentially. This ensures that there are no partially complete operations
// when a new operation starts.
class OperationQueue : public OperationContainer {
 public:
  OperationQueue();
  ~OperationQueue() override;

 private:
  fxl::WeakPtr<OperationContainer> GetWeakPtr() override;
  void Hold(OperationBase* o) override;
  void Drop(OperationBase* o) override;
  void Cont() override;

  // Are there any operations running? An operation is considered running once
  // its |Run()| has been invoked, up until result callback has finished. Note
  // that an operation could be running while it is not present in
  // |operations_|: its result callback could be executing.
  bool idle_ = true;

  std::queue<std::unique_ptr<OperationBase>> operations_;

  // It is essential that the weak_ptr_factory is defined after the operations_
  // container, so that it gets destroyed before operations_, and hence before
  // the Operation instances in it, so that the Done() call on their
  // OperationBase sees the container weak pointer to be null. That's also why
  // we need the virtual GetWeakPtr() in the base class, rather than to put the
  // weak ptr factory in the base class.
  fxl::WeakPtrFactory<OperationContainer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(OperationQueue);
};

// Something that can be put in an OperationContainer until it calls Done() on
// itself. Used to implement asynchronous operations that need to hold on to
// handles until the operation asynchronously completes and returns a value.
//
// Held by a unique_ptr<> in the OperationContainer, so instances of derived
// classes need to be created with new.
//
// Advantages of using an Operation instance to implement asynchronous fidl
// method invocations:
//
//  1. To receive the return callback, the interface pointer on which the method
//     is invoked needs to be kept around. It's tricky to place a move-only
//     interface pointer on the capture list of a lambda passed as argument to
//     its own method. An instance is much simpler.
//
//  2. The capture list of the callbacks only holds this, everything else that
//     needs to be passed on is in the instance.
//
//  3. Completion callbacks don't need to be made copyable and don't need to be
//     mutable, because no move only pointer is pulled from their capture list.
//
//  4. Conversion of Handle to Ptr can be done by Bind() because the Ptr is
//     already there.
//
// Use of Operation instances must adhere to invariants:
//
// * Deleting the operation container deletes all Operation instances in it, and
//   it must be guaranteed that no callbacks of ongoing method invocations are
//   invoked after the Operation instance they access is deleted. In order to
//   accomplish this, an Operation instance must only invoke methods on fidl
//   pointers that are either owned by the Operation instance, or that are owned
//   by the immediate owner of the operation container. This way, when the
//   operation container is deleted, the destructors of all involved fidl
//   pointers are called, close their channels, and cancel all pending method
//   callbacks. If a method is invoked on a fidl pointer that lives on beyond
//   the lifetime of the operation container, this is not guaranteed.
class OperationBase {
 public:
  virtual ~OperationBase();

  // Derived classes implement this method. It is called by the container of
  // this Operation when it is ready. At some point after Run() is invoked,
  // |Done()| must be called to signal completion of this operation. This
  // usually happens implicitly from the destructor of a FlowToken that is
  // passed along the asynchronous flow of control of the Operation.
  virtual void Run() = 0;

  // Needed to guard callbacks to methods on FIDL pointers that are not owned by
  // this Operation instance.
  //
  // Callbacks on methods on FIDL pointers that are owned by Operation instance
  // are never invoked after the Operation instance is deleted, because they are
  // cancelled by the destructor of the FIDL pointer. However, if the FIDL
  // pointer is owned outside of the Operation instance, such a callback may be
  // invoked after the Operation instance was destroyed, if the FIDL pointer
  // lives longer.
  fxl::WeakPtr<OperationBase> GetWeakPtr();

 protected:
  // |trace_name| and |trace_info| are used to annotate performance traces.
  // |trace_name| must outlive *this.
  OperationBase(const char* trace_name, std::string trace_info);

  // Useful in log messages.
  const char* trace_name() const { return trace_name_; }

  class FlowTokenBase;

 private:
  // OperationContainer calls SetOwner(), InvalidateWeakPtrs() and Schedule().
  friend class OperationContainer;

  // Called only by OperationContainer::Add().
  void SetOwner(OperationContainer* c);

  // Called only by OperationContainer.
  void Schedule();

  // Called only by OperationContainer.
  void InvalidateWeakPtrs();

  // Operation<..> class DispatchCallback() and accesses trace info fields
  // below.
  template <typename... Args>
  friend class Operation;

  // Called only by Operation<...>.
  template <typename... Args,
            typename ResultCall = std::function<void(Args...)>>
  void DispatchCallback(ResultCall result_call, Args... result_args) {
    // Move |container| pointer out of this, because |this| gets deleted before
    // we stop using |container|.
    auto container = std::move(container_);
    if (container) {
      // Deletes |this|.
      container->Drop(this);

      // Can no longer refer to |this|.
      result_call(std::move(result_args)...);

      // The result callback may cause the container to be deleted, so we must
      // check it still exists before telling it to continue.
      if (container) {
        container->Cont();
      }
    }
  }

  // Traces the duration of the Operation excution. Begin() is called from
  // Schedule(), End() is called from Done().
  void TraceAsyncBegin();
  void TraceAsyncEnd();

  fxl::WeakPtr<OperationContainer> container_;

  // Used by FlowTokenBase to suppress Done() calls after the Operation instance
  // is deleted. The OperationContainer will invalidate all weak pointers to
  // this instance before the destructor is invoked, so it's not necessary that
  // this field is last.
  fxl::WeakPtrFactory<OperationBase> weak_ptr_factory_;

  // Name used to label traces for this operation.
  const char* const trace_name_;

  // Unique identifier used to correlate trace events for this operation.
  const uint64_t trace_id_;

  // Additional information added to trace events for this operation.
  const std::string trace_info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(OperationBase);
};

template <typename... Args>
class Operation : public OperationBase {
 public:
  ~Operation() override = default;

  using ResultCall = std::function<void(Args...)>;

 protected:
  Operation(const char* const trace_name, ResultCall result_call,
            const std::string& trace_info = "")
      : OperationBase(trace_name, trace_info),
        result_call_(std::move(result_call)) {}

  // Derived classes call this when they are prepared to be removed from the
  // container. Must be the last thing this instance does, as it results in
  // destructor invocation.
  void Done(Args... result_args) {
    TraceAsyncEnd();
    DispatchCallback(std::move(result_call_), std::move(result_args)...);
  }

  class FlowToken;
  class FlowTokenHolder;

 private:
  ResultCall result_call_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Operation);
};

// The instance of FlowToken at which the refcount reaches zero in the
// destructor calls Done() on the Operation it holds a reference of.
//
// It is an inner class of Operation so that it has access to Done(), which is
// protected.
//
// Why this is ref counted, not moved:
//
// 1. Some flows of control branch into multiple parallel branches, and then its
//    subtle to figure out which one to move it along.
//
// 2. To move something onto a capture list is more verbose than to just copy
//    it, so it would defeat the purpose of being simpler to write than the
//    status quo.
//
// 3. A lambda with something that is moveonly on its capture list is not
//    copyable anymore, and one of the points of Operation was to only have
//    copyable continuations.
//
// NOTE: You cannot mix flow tokens and explicit Done() calls. Once an Operation
// uses a flow token, this is how Done() is called, and calling Done()
// explicitly would call it twice.
//
// The parts that are not dependent on the template parameter are factored off
// into a base class as usual.
class OperationBase::FlowTokenBase {
 public:
  explicit FlowTokenBase(OperationBase* op);
  FlowTokenBase(const FlowTokenBase& other);
  ~FlowTokenBase();

 protected:
  int refcount() const { return *refcount_; }
  bool weak_op() const { return weak_op_.get() != nullptr; }

 private:
  int* const refcount_;  // shared between copies of FlowToken.
  fxl::WeakPtr<OperationBase> weak_op_;
};

template <typename... Args>
class Operation<Args...>::FlowToken : OperationBase::FlowTokenBase {
 public:
  explicit FlowToken(Operation<Args...>* const op, Args* const... result)
      : FlowTokenBase(op), op_(op), result_(result...) {}

  FlowToken(const FlowToken& other)
      : FlowTokenBase(other), op_(other.op_), result_(other.result_) {}

  ~FlowToken() {
    // If refcount is 1 here, it will become 0 in ~FlowTokenBase.
    if (refcount() == 1 && weak_op()) {
      apply(std::make_index_sequence<
            std::tuple_size<decltype(result_)>::value>{});
    }
  }

 private:
  // This usage is based on the implementation of std::apply(), which is only
  // available in C++17: http://en.cppreference.com/w/cpp/utility/apply
  template <size_t... I>
  void apply(std::integer_sequence<size_t, I...> /*unused*/) {
    op_->Done(std::move((*std::get<I>(result_)))...);
  }

  Operation<Args...>* const op_;  // not owned

  // The pointers that FlowToken() is constructed with are stored in this
  // std::tuple. They are then extracted and std::move()'d in the |apply()|
  // method.
  std::tuple<Args* const...> result_;  // the values are not owned
};

// Sometimes the asynchronous flow of control that is represented by a FlowToken
// branches, but is actually continued on exactly one branch. For example, when
// a method is called on an external fidl service, and the callback from that
// method is also scheduled as a timeout to avoid blocking the operation queue
// in case the external service misbehaves and doesn't respond.
//
// In that situation, it would be wrong to place two copies of the flow token on
// the capture list of both callbacks, because then the flow would only be
// Done() once both callbacks are destroyed. In the case where the second
// callback is a timeout, this might be really late.
//
// Instead, a single copy of the flow token is placed in a shared container, a
// reference to which is placed on the capture list of both callbacks. The first
// callback that is invoked removes the flow token from the shared container and
// propagates it from there.
//
// That way, the flow token in the shared container also acts as call-once flag.
//
// The flow token holder is a simple wrapper of a shared ptr to a unique ptr. We
// define it because such a nested smart pointer is rather unwieldy to write
// every time.
//
// Example use:
//
//   FlowTokenHolder branch{flow};
//
//   auto kill_agent = [this, branch] {
//     std::unique_ptr<FlowToken> flow = branch.Continue();
//     if (!flow) {
//       return;
//     }
//
//     stopped_ = true;
//   };
//
//   StopAgent(kill_agent);
//   SetTimeout(kill_agent, 1);
//
template <typename... Args>
class Operation<Args...>::FlowTokenHolder {
 public:
  using FlowToken = Operation<Args...>::FlowToken;

  explicit FlowTokenHolder(const FlowToken& flow)
      : ptr_(std::make_shared<std::unique_ptr<FlowToken>>(
            std::make_unique<FlowToken>(flow))) {}

  FlowTokenHolder(const FlowTokenHolder& other) : ptr_(other.ptr_) {}

  // Calling this method again on any copy of the same FlowTokenHolder yields a
  // nullptr. Clients can check for that to enforce call once semantics.
  //
  // The method is const because it only mutates the pointed to object. It's
  // useful to exploit this loophole because this way lambdas that have a
  // FlowTokenHolder on their capture list don't need to be mutable.
  std::unique_ptr<FlowToken> Continue() const { return std::move(*ptr_); }

 private:
  std::shared_ptr<std::unique_ptr<FlowToken>> ptr_;
};

// Following is a list of commonly used operations.

// An operation which wraps a Future<>. See WrapFutureAsOperation() below.
template <typename... Args>
class FutureOperation : public Operation<Args...> {
 public:
  using ResultCall = std::function<void(Args...)>;

  FutureOperation(const char* trace_name, FuturePtr<> on_run,
                  FuturePtr<Args...> done, ResultCall result_call)
      : Operation<Args...>(trace_name, std::move(result_call)),
        on_run_(on_run),
        done_(done) {}

 private:
  // |OperationBase|
  void Run() {
    // FuturePtr is a shared ptr, so the Then() callback is not necessarily
    // cancelled by the destructor of this Operation instance. Hence the
    // callback must be protected against invocation after delete of this.
    done_->WeakThen(this->GetWeakPtr(), [this](Args&&... args) {
      this->Done(std::forward<Args>(args)...);
    });
    on_run_->Complete();
  }

  FuturePtr<> on_run_;
  FuturePtr<Args...> done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FutureOperation);
};

// EXPERIMENTAL
//
// This is useful to glue a FIDL call that expects its result as a callback with
// a Future<>, but have the business logic of the Future<> run on an
// OperationContainer.
//
// Usage:
//
// void MyFidlCall(args..., MyFidlCallResult result_call) {
//   auto on_run = Future<>::Create();
//   auto done = on_run->Map(...)->Then(...);
//   operation_container_.Add(WrapFutureAsOperation(
//       "MyFidlCall", on_run, done, result_call));
// }
//
template <typename... ResultArgs, typename... FutureArgs>
OperationBase* WrapFutureAsOperation(
    const char* const trace_name, FuturePtr<> on_run,
    FuturePtr<FutureArgs...> done,
    std::function<void(ResultArgs...)> result_call) {
  return new FutureOperation<ResultArgs...>(
      trace_name, std::move(on_run), std::move(done), std::move(result_call));
}

template <typename... Args>
class FutureOperation2 : public Operation<Args...> {
 public:
  using ResultCall = std::function<void(Args...)>;
  using RunOpCall = std::function<FuturePtr<Args...>(OperationBase*)>;

  FutureOperation2(const char* const trace_name, RunOpCall run_op,
                   ResultCall done)
      : Operation<Args...>(trace_name, std::move(done)), run_op_(run_op) {}

 private:
  // |OperationBase|
  void Run() {
    // FuturePtr is a shared ptr, so the Then() callback is not necessarily
    // cancelled by the destructor of this Operation instance. Hence the
    // callback must be protected against invocation after delete of this.
    run_op_(this)->WeakThen(this->GetWeakPtr(), [this](Args&&... args) {
      this->Done(std::forward<Args>(args)...);
    });
  }

  RunOpCall run_op_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FutureOperation2);
};

// EXPERIMENTAL
//
// Glue code to define operations where the body of the operation is defined
// inline to where the operation instance is created. It is an alternative to
// WrapFutureAsOperation.
//
// The body of the operation is defined as a callback, and is expected to return
// a Future<> that is completed when the operation is finished. If the Future<>
// is completed with values, those values are used as the result of the
// Operation.
//
// Usage:
//
// void MyFidlCall(args..., MyFidlCallResult result_call) {
//   operation_container.Add(NewCallbackOperation(
//      "MyFidlService::MyFidlCall",
//      [this] (OperationBase* op) {
//        // Use |op->GetWeakPtr()| to guard any async calls.
//        auto f = Future<>::Create();
//
//        DoSomethingAsync(f->Completer());
//
//        f->WeakMap(op->GetWeakPtr(), [] (int do_something_result) {
//          return do_something_result * 2;
//        });
//
//        return f;
//      },
//      result_call);
// }
template <typename... ResultArgs>
OperationBase* NewCallbackOperation(
    const char* const trace_name,
    typename FutureOperation2<ResultArgs...>::RunOpCall run,
    typename FutureOperation2<ResultArgs...>::ResultCall done) {
  return new FutureOperation2<ResultArgs...>(trace_name, std::move(run),
                                             std::move(done));
}

// An operation which immediately calls its result callback. This is useful for
// making sure that all operations that run before this have completed.
class SyncCall : public Operation<> {
 public:
  SyncCall(ResultCall result_call)
      : Operation("SyncCall", std::move(result_call)) {}

  void Run() override { Done(); }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(SyncCall);
};

}  // namespace modular

#endif  // LIB_ASYNC_CPP_OPERATION_H_
