# Thread safe asynchronous code

Writing correct asynchronous programs with multiple threads requires care in
C++. Here we describe a particular pattern that helps avoid errors, and which
will integrate well with the C++ FIDL bindings and component runtime.

## Background

### Asynchronous runtimes

The [async][async-readme] library defines the *interface*
for initiating asynchronous operations on Fuchsia. It defines an opaque
`async_dispatcher_t` type, and associated functions.

There are several *implementations* of this dispatcher interface. A popular one
is [`async_loop_t`][async-loop] and its C++ wrapper
[`async::Loop`][async-loop-cpp]. Libraries that performs asynchronous work
generally should not know what is the concrete implementation. Instead they
would call functions over the `async_dispatcher_t*` interface.

### Thread safety

The reader should familiarize themselves with the terminology around
[thread safety][thread-safety] if needed.

A program that upholds thread safety avoids data races: broadly, reading and
writing the same data without a defined ordering between those operations (see
precise definition of a [data race][data-race] in the C++ standard). These races
are a source of errors because they lead to undefined behavior at run-time.

An individual C++ type also has categorizations around thread-safety. Referring
common practice interpretations from [abseil][abseil-thread-safety]:

- A C++ object is *thread-safe* if concurrent usages does not cause data races.
- A C++ object is *thread-unsafe* if any concurrent usage may cause data races.

One may wrap a thread-unsafe type with *synchronization primitives* e.g. mutexes
to make it thread-safe. This is called adding *external synchronization*. Doing
so adds overhead, and not all users will use that type concurrently. Hence it's
common for a library to be thread-unsafe by default, and require the user to add
synchronization if desired. Such types may have comments like the following:

```c++
// This class is thread-unsafe. Methods require external synchronization.
class SomeUnsafeType { /* ... */ };
```

## Achieving thread safety in asynchronous code

Achieving thread safety gets more subtle in asynchronous code due to the
presence of callbacks. Consider the following snippet:

```c++
// |CsvParser| asynchronously reads from a file, and parses the contents as
// comma separated values.
class CsvParser{
 public:
  void Load() {
    reader_.AsyncRead([this] (std::string data) {
      values_ = Parse(data);
    });
  }

  std::vector<std::string> Parse(const std::string& data);

 private:
  FileReader reader_;
  std::vector<std::string> values_;
};
```

`AsyncRead` will complete the work in the background, then call the lambda
specified as the callback function when the work completes. Because the lambda
captures `this`, it is commonly referred to as an "upcall": the `reader_` that
is owned by an instance of `CsvParser` makes a call to the owner.

Let's consider how to avoid races between this callback and the destruction of
`CsvParser`. Adding a mutex in `CsvParser` won't help, because the mutex would
be destroyed if `CsvParser` is destroyed. One may require that `CsvParser` must
always be reference counted, but that results in an opinionated API and tends to
recursively cause everything referenced by `CsvParser` to also be reference
counted.

If we ensure that there is always a defined ordering between the destruction of
`CsvParser` and the invocation of the callback, then the race condition is
avoided. On Fuchsia, the callback is typically scheduled on an
`async_dispatcher_t` object. A common pattern is to use a single threaded
dispatcher:

- Use an `async::Loop` as the dispatcher implementation.
- Only run one thread to service the loop.
- Only destroy upcall targets on that thread. For example, destroy the
  `CsvParser` within a task posted to that dispatcher.

Since the same thread invokes asynchronous callbacks and destroys the instance,
there must be a defined ordering between those operations.

This scenario is common across Fuchsia C++ because FIDL server components are
strongly encouraged to be concurrent and asynchronous. See
[C++ FIDL threading guide][cpp-threading-guide] for a concrete discussion of
this scenario when using FIDL bindings.

### Sequences

More generally, if a dispatcher promises that tasks posted on that dispatcher
always run with a defined ordering, it is safe to destroy upcall targets on a
dispatcher task, and synchronization with upcalls is guaranteed. Such
dispatchers are said to support *sequences*: sequential execution domains which
runs a series of tasks with strict mutual exclusion, but where the underlying
execution may hop from one thread to another.

Synchronized driver dispatchers ([`fdf::Dispatcher`][fdf-dispatcher] created
with the `FDF_DISPATCHER_OPTION_SYNCHRONIZED` option) are an example of sequence
supporting dispatchers.

When using dispatchers supporting sequences, a common pattern for ensuring
thread safety is to use the object from a single sequence.

### Enforce with runtime checks {#mutual-exclusion-guarantee}

We provide libraries for enforcing the above common patterns at runtime. In
particular, thread-unsafe types may check that an instance is always used from
an asynchronous dispatcher that ensures ordering between operations. Here "used
from" covers construction, destruction, and calling instance methods. We call
this *mutual exclusion guarantee*. Specifically:

- If the `async_dispatcher_t` supports [*sequences*](#sequences), then code
  running on tasks posted to that dispatcher are ordered with one another.
- If the `async_dispatcher_t` does not support sequences, then code running on
  tasks posted to that dispatcher are ordered if that dispatcher is only
  serviced by a single thread, for example, a single-threaded `async::Loop`.

In short, either the dispatcher supports sequences in which case the object must
be used on that sequence, or the code runs on a single dispatcher thread and the
object must be used on that thread.

The async library offers a BasicLockable type,
[`async::synchronization_checker`](/zircon/system/ulib/async/include/lib/async/cpp/sequence_checker.h).
You may call `.lock()` or lock the checker using a `std::lock_guard` whenever a
function requires mutual exclusion. Doing so checks that the function is called
from a dispatcher with such a guarantee, without actually taking any locks. Here
is a full example:

```cpp
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/cpp/synchronization_checker/main.cc" region_tag="synchronization_checker" adjust_indentation="auto" exclude_regexp="^TEST|^}" %}
```

`fidl::Client` is another example of types that check for mutual exclusion
guarantee at runtime: destroying a `fidl::Client` on a non-dispatcher thread
will lead to a panic.

### Discard callbacks during destruction

You may have noticed that for the `ChannelReader` example above to work, the
callback passed to `wait_.Begin(...)` must be silently discarded, instead of
called with some error, if `ChannelReader` is destroyed. Indeed the
[documentation][async-wait] on `async::WaitOnce` mentions that it "automatically
cancels the wait when it goes out of scope".

During destruction, some C++ objects would discard the registered callbacks if
those have yet to be called. These kind of APIs are said to guarantee *at most
once delivery*. `async::Wait` and `async::Task` are examples of such objects.
This style works well when the callback references a single receiver that owns
the wait/task, i.e. the callback is an upcall. These APIs are typically also
thread-unsafe and requires the aforementioned mutual exclusion guarantee.

Other objects will always call the the registered callback exactly once, even
during destruction. Those calls would typically provide an error or status
indicating cancellation. They are said to guarantee *exactly once delivery*.

One should consult the corresponding documentation when using an
asynchronous API to understand the cancellation semantics.

It is possible to convert an *exactly once* API into an *at most once* API by
discarding the upcall if the object making the upcalls is already destroyed.
[`closure-queue`][closure-queue] is a library that implements this idea;
destroying a `ClosureQueue` will discard unexecuted callbacks scheduled on that
queue.

### Use an object with different mutual exclusion requirements

To maintain the mutual exclusion guarantees, one may manage and use a group of
objects on the same sequence (if supported) or single threaded dispatcher. Those
objects can synchronously call into one another without breaking the mutual
exclusion runtime checks. A special case of this is an application that runs
everything on a single `async::Loop` with a single thread, typically called the
main thread.

More complex applications may have multiple sequences or multiple single
threaded dispatchers. When individual objects must be used from their
corresponding sequence or single threaded dispatcher, a question arises: how
does one object call another object if they are associated with different
dispatchers?

A time-tested approach is to have the objects send messages between one another,
as opposed to synchronously calling their instance methods. Concretely, this
could mean that if object `A` needs to do something to object `B`, `A` would
post an asynchronous task to `B`'s dispatcher using `async::PostTask`. The task
(usually a lambda function) may then synchronously use `B` because it is already
running under `B`'s mutual exclusion guarantee.

When tasks are posted to a different dispatcher, it's harder to safely discard
them when the receiver object goes out of scope. Here are some approaches:

- One may shutdown the dispatcher before destroying the object, if that
  dispatcher serves exactly that object. For example, `B` may own an
  `async::Loop` as the last member field. When `B` destructs, the `async::Loop`
  would be destroyed, which silently discards any unexecuted tasks posted to
  `B`.
- One may reference count the objects, and pass a weak pointer to the posted
  task. The posted task should do nothing if the pointer is expired.

Golang is a popular [example][golang] that baked this principle into their
language design.

## Prior arts

Lightweight mechanisms of ensuring a set of tasks execute one after the other,
without necessarily starting operating system threads, is a recurring theme:

- The Chromium project defines a similar sequence concept: [Threading and
tasks in Chrome][chrome].
- The Java Platform added [virtual threads][java].

[async-readme]: /zircon/system/ulib/async/README.md
[async-loop]: /zircon/system/ulib/async-loop/include/lib/async-loop/loop.h
[async-loop-cpp]: /zircon/system/ulib/async-loop/include/lib/async-loop/cpp/loop.h
[async-wait]: /zircon/system/ulib/async/include/lib/async/cpp/wait.h
[fdf-dispatcher]: /sdk/lib/driver/runtime/include/lib/fdf/cpp/dispatcher.h
[thread-safety]: https://en.wikipedia.org/wiki/Thread_safety
[data-race]: http://eel.is/c++draft/intro.races#21
[abseil-thread-safety]: https://abseil.io/blog/20180531-regular-types#data-races-and-thread-safety-properties
[cpp-threading-guide]: /docs/development/languages/fidl/tutorials/cpp/topics/threading.md
[closure-queue]: /zircon/system/ulib/closure-queue/include/lib/closure-queue/closure_queue.h
[chrome]: https://chromium.googlesource.com/chromium/src/+/master/docs/threading_and_tasks.md
[java]: https://openjdk.org/jeps/425
[golang]: https://go.dev/blog/codelab-share
