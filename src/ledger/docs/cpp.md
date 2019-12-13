# C++ in Ledger

Overview of the idioms, patterns and libraries specific to Ledger C++ codebase.
The intention is to not duplicate information already present in the individual
header files, but rather to provide a guided tour of the most interesting header
files with TL:DR of when to use each.

[TOC]

## coroutines

The Ledger codebase (and Fuchsia in general) is highly asynchronous, making the code
prone to the “callback hell” – highly nested and difficult to follow code that
executes async operations in response to async operations.

The standard solution to this in languages such as Dart or Python is
async/await, allowing to concisely express the fact that a step of the function
is asynchronous without the additional level of nesting. However, C++ doesn’t
have a similar canonical solution.

As some parts of the Ledger codebase need to perform particularly complex chains of
async operations, we developed a custom solution that approximates async/await
using coroutines. This allows us to write code like this:


```cpp
if (SyncCall(handler, &LongAsyncComputation, &s, &i) ==
    ContinuationStatus::INTERRUPTED) {
  return ContinuationStatus::INTERRUPTED;
}
LEDGER_LOG(INFO) << "LongAsyncComputation returned: " << s << " " << i;
// Possibly make further async calls.
```

Instead of:

```cpp
LongAsyncComputation([] (auto s, auto i) {
  LEDGER_LOG(INFO) << "LongAsyncComputation returned: " << s << " " << i;
  // Possibly make further async calls.
});
```


Coroutines make it possible to implement SyncCall by allowing us to pause the
current execution context of a thread (freeing the thread to do other things)
and resume it later. The implementation of coroutines we use is custom
(Fuchsia-specific) and lives under //src/ledger/lib/coroutine/coroutine.h .

When working with coroutines:

*   manage them using coroutine_manager.h – this util helps to make sure that
    coroutines that refer to this object are shut down when the object goes
    away;
*   use coroutine::SyncCall to await for results of an async call. You should
    not need to manually yield or resume coroutines.
*   when SyncCall returns ContinuationStatus::INTERRUPTED, return immediately.

## callback library

Ledger makes extensive use of the [callback] library. It is a collection of
utilities we found useful for developing Ledger – they were originally written
by the Ledger team but now live outside of //src/ledger because other teams
wanted to use them.

***note
Note – do not confuse the `callback` library with `fit::callback`, which is
a single util within the [fit] library under `//zircon/system/ulib/fit`).
***

**Containers**:

*   [auto_cleanable.h] provides two container types which “automatically” manage
    deleting elements from them: AutoCleanableMap and AutoCleanableSet.
    “Automatic” here means that all elements stored in the containers are given
    an “OnDiscardable” callback (via a
    `void SetOnDiscardable(fit::closure on_discardable)` method that they must
    implement) – when this callback is called, they are deleted from the parent
    container. Useful for building hierarchical collections of objects.
    *   TODO: add a file-level comment in this header
*   [managed_container.h] – TODO: what is it for?

Facilities that help manage **lifetime**:

*   [cancellable.h] – can be used as a return type of an asynchronous function
    if the caller needs to be able to cancel the operation. The caller can then
    use a CancellableContainer to store pending cancellable calls that it is
    making – when the caller is deleted, the cancellable operations are
    automatically cancelled.
    *   **tip**: this is useful only if there is actual meaningful cancellation
        that can happen – if you need to just ignore the resulting callback, use
        “scoped_callback” below
*   [scoped_task_runner.h] and [scoped_callback.h] – useful when an object posts
    tasks that refer to the object itself, but could end up being executed after
    the object itself is deleted.
    *   [scoped_callback.h] allows to wrap any callback in a lambda that, when
        called, executes the initial callback only if a given “witness” is still
        true – by using a weak pointer to our object, we can ensure that stale
        callbacks are not executed.
        *   TODO: add a file-level comment in this header
    *   [scoped_task_runner.h] manages this automatically – it’s a wrapper over
        a regular task runner than wraps each posted task into a scoped callback
        with a weak pointer to the scoped task runner itself.
*   [destruction_sentinel.h] – useful when implementing an object method that
    may directly or indirectly delete the object itself within the method body
    (e.g. by triggering the cleanup of the object from the container in which it
    is stored)

Various utils related to callbacks:

*   [operation_serializer.h] – this is “serialization” in the sense of executing
    one after another, not in the sense of producing a binary representation.
    The util takes async operations and executes each only if all the previously
    inserted operations have already completed
*   [trace_callback.h] – wraps a callback in tracing instrumentation
*   [waiter.h] – utilities that collect results of multiple asynchronous
    operations into a single asynchronous callback that is called when all
    results are ready. This includes:
    *   Waiter – collects results of multiple callbacks producing type `A` into
        `std::vector<A>`
    *   AnyWaiter – collects results of multiple callbacks producing type `A`
        into the first result available
    *   StatusWaiter – collects results of multiple callbacks producing only
        completion status `S` into status OK if all of them succeeded, or the
        first error status if any of them failed
*   [ensure_called.h] – ensures that a function is called whatever happens with
    default arguments, but can also be called manually with specific arguments,
    and its result can be retrieved. This is similar but more general than
    [defer.h], which is never called manually.
    *   TODO: add a file-level comment in this header

## FIDL

Types generate from FIDL files are a bit unwieldy (e.g.
`fuchsia::ledger::cloud::CloudProvider`). We use header files that define
aliases for them, e.g.:

```cpp
namespace cloud_provider {
  using CloudProvider = fuchsia::ledger::cloud::CloudProvider;
}
```

See [example header](/src/ledger/bin/fidl/include/types.h).

## testing

Most tests are based on [test_loop_fixture.h], which abstracts the time and
provides a useful “RunLoopUntilIdle()” method.

Together with the “callback” library helpers `Capture` and `SetWhenCalled` (see
[capture.h]) and [set_when_called.h]), this provides a handy pattern for
concisely verifying results of an async call:


```cpp
 storage->GetCommit(
     parent_id,
     Capture(SetWhenCalled(&called), &status, &base));
 RunLoopUntilIdle();
 EXPECT_TRUE(called);
 EXPECT_EQ(storage::Status::OK, status);
```

The tests use the main function defined in
//src/lib/fxl/test/run_all_unittests.cc. This provides additional features:

*   control log settings via --verbose, --quiet and --log_file,
*   control TestLoop's random seed via --test_seed_loop to easily reproduce a
    random-related flakiness (the random seed is logged whenever a test is
    started)

See [Testing](testing.md) for documentation on different types of tests and how
to run them.

[auto_cleanable.h]: /src/lib/callback/auto_cleanable.h
[callback]: /src/lib/callback
[cancellable.h]: /src/lib/callback/cancellable.h
[capture.h]: /src/ledger/lib/callback/capture.h
[defer.h]: /zircon/system/ulib/fit/include/lib/fit/defer.h
[destruction_sentinel.h]: /src/lib/callback/destruction_sentinel.h
[ensure_called.h]: /src/ledger/lib/callback/ensure_called.h
[fit]: /zircon/system/ulib/fit
[managed_container.h]: /src/ledger/lib/callback/managed_container.h
[operation_serializer.h]: /src/ledger/lib/callback/operation_serializer.h
[scoped_callback.h]: /src/lib/callback/scoped_callback.h
[scoped_task_runner.h]: /src/lib/callback/scoped_task_runner.h
[scoped_task_runner.h]: /src/lib/callback/scoped_task_runner.h
[set_when_called.h]: /src/ledger/lib/callback/set_when_called.h
[test_loop_fixture.h]: /src/lib/testing/loop_fixture/test_loop_fixture.h
[trace_callback.h]: /src/ledger/lib/callback/trace_callback.h
[waiter.h]: /src/ledger/lib/callback/waiter.h
