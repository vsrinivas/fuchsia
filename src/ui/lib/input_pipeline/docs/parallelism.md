# input_pipeline > Parallelism

Reviewed on: 2022-03-22

## Design

The input pipeline library is single-threaded. More precisely:

* `input_handler::InputHandler::handle_input_event()` is not [`Send`](https://doc.rust-lang.org/std/marker/trait.Send.html).

* `input_pipeline::InputPipelineAssembly` is not `Send`. This is because
  * `handle_input_event()` is not `Send`, which means that
  * the `Task`s created to run `handle_input_event()` futures are not `Send`,
    which means that
  * the `Vec` of `Task`s in `InputPipeline` is not `Send`, which means that
  * `InputPipelineAssembly` is not `Send`.

* `input_pipeline::InputPipeline` is not `Send`. This is analogous to
  the argument for `InputPipelineAssembly`.

## Rationale

1. Meeting the `Send` requirement is hard.

   quiche@ forgets the exact details, but remembers that meeting `Send` was one
   of the blockers to landing [`FactoryResetHandler`](https://fuchsia-review.googlesource.com/c/fuchsia/+/544827).

1. Running on a `SendExecutor` makes it harder to reason about correctness.

   With a `SendExecutor`, a `Future` may be preempted to run another `Future`
   that accesses the same data. Or another thread may access the same data
   simultaneously.

   While the Rust compiler will verify that these calls are memory-safe,
   the code may still have deadlocks, or semantic errors.

   For example:
   1. The code may have deadlocks due to inconsistent locking orders of different
      methods on a struct.
   1. The code may be semantically incorrect, if it reads a value, caches it,
      releases a lock, and then uses the cached value to update state after
      re-acquiring the lock.

   The deadlock and semantic error risks aren't unique to `SendExecutor`s, but
   they're harder to check for with `SendExecutor`s. This is because, with a
   `SendExecutor` the data access from the other `Future` might occur at any
   moment (outside of critical regions, of course).

   In contrast, with a `LocalExecutor`, a `Future` will run until it yields (by
   invoking the `await` operator). This greatly reduces the locations (line
   of code) where the program might have conflicting actions by multiple
   `Future`s accessing the same data.

1. It's unlikely that multithreading would improve input pipeline performance
   (and might make it worse).

   `InputHandler`s don't do a lot of computation, and all of their I/O is
   asynchronous. This limits the speed-up that we might get from executing
   handlers in parallel.

   On the other hand, multithreading introduces costs:
   1. Using atomic primitives (`Arc` instead of `Rc`, and `Mutex` instead of
      `RefCell`) requires synchronization between processors.
   2. If two handlers are scheduled on cores that don't share a cache, data
      may have to be copied between caches.

1. Multithreading makes programs a greater risk to system health.

   When a single threaded program gets stuck in an infinite loop, it can only
   consume a single core. A multi-threaded program can consume multiple cores.