# Coroutine library

## Description
This coroutine library can help you write highly asynchronous code, and avoid
"callback hell". It does so by allowing a function (or method) to be paused and
resumed by swapping [execution contexts](context/context.h). It makes the code
*look* synchronous, while remaining asynchronous under the hood, with all the
usual concurrency pitfalls, such as race conditions and deadlocks.

Coroutines are different than multithreading, but both can be used together.
See [CoroutineHandler](coroutine.h) for details.

## Usage

Here are some tips on good use of the Coroutine library.

### Use CoroutineManager in classes

Usually, coroutines created within a class object should not survive its
destruction, whether because continuing the processing didn't make sense, or
because resources captured by the coroutine would be destroyed (such as `this`).

[CoroutineManager](coroutine_manager.h) is a proxy class for
[CoroutineService](coroutine.h). `CoroutineManager` interrupts the coroutine it
created when destroyed, and can be created using the `CoroutineService` vended
by an `Environment` object.

You should consider using `CoroutineManager` if you use coroutines in your
class.

Free-standing functions probably don't need `CoroutineManager` and can use
`CoroutineService` directly.

### When receiving INTERRUPTED, return

`coroutine::ContinuationStatus::INTERRUPTED` means another part of your code
requested the coroutine to terminate gracefully. This would be the case if the
`CoroutineManager` or `CoroutineService` who created this coroutine are
destroyed.

This mechanism is needed because other parts of the program don’t know the heap
allocations made inside the coroutine, as well as other cleanup performed by
the destructors of objects created or owned by the coroutine. When a coroutine
destruction is needed, it is resumed with an `ContinuationStatus::INTERRUPTED`
and it is the coroutine's job to unwind its call stack.

Usually, the only thing you need to do when receiving a
`ContinuationStatus::INTERRUPTED` is to return immediately. Doing more work is
dangerous as some objects you rely on may be destroyed already.


### Don’t use Yield and Resume

You probably don’t need to use `Yield()` and `Resume()` directly.
[SyncCall](coroutine.h) is a utility function that can be used to wrap any
asynchronous call, so that you don't have to use `CoroutineHandler` methods
directly.

If you have an asynchronous function with the signature
`AsynchronousCall(Argument, fit::function<void(Status, Result)>)`, then you can
wrap it such as:
``` cpp
Argument argument(...)
Status status;
Result value;
if (coroutine::SyncCall(handler,
    [argument](fit::function<void(Status, Result)> cb) {
        AsynchronousCall(argument, std::move(cb));
    }, &status, &value) == coroutine::ContinuationStatus::INTERRUPTED) {
  return Status::INTERRUPTED;
}
if (status != Status::OK)
  return status;
Process(value);
```

`SyncCall` will ensure the asynchronous call is made and the coroutine paused,
and then resumed when the asynchronous callback is executed.


### Use coroutine::Wait with for loops

Coroutines make it very easy to write asynchronous code, but the execution of
the coroutine itself remains sequential. In particular, `for` loops are not run
in parallel. If you can, avoid the following pattern:
``` cpp
std::vector<Result> results;
for (auto& obj : objects_) {
  Result result;
  // Don't do that! SynchronousFrobinate does not need to wait for the previous
  // call to finish.
  if (SynchronousFrobinate(handler, obj, &result) ==
      coroutine::ContinuationStatus::INTERRUPTED) {
    return;
  }
  results.push_back(std::move(result));
}
```

Instead, make the asynchronous calls directly and use
[coroutine::Waiter](coroutine_waiter.h) to collate the results:
``` cpp
auto waiter = fxl::MakeRefCounted<
    Waiter<Status, std::unique_ptr<Result>>>(Status::OK);

for (auto& obj : objects_) {
  AsyncFrobinate(obj, waiter->NewCallback());
}

Status status; std::vector<std::unique_ptr<Result>> result;
if (coroutine::Wait(handler, std::move(waiter), &s, &result) ==
    coroutine::ContinuationStatus::INTERRUPTED) {
  return Status::INTERRUPTED;
}
```
