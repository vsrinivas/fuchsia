# inject

The `inject` library simplifies the scaffolding required to start and stop
applications, and in particular, command line tools.

## Use

With the `inject` library, the key pieces of an application which carry global
state -- think flags, parsed configuration -- are represented as individual
pieces dubbed **modules**.

Each module is responsible for its little piece of the world, and its scope
should be small. When creating a module, think about the [principle of least
knowledge](https://en.wikipedia.org/wiki/Law_of_Demeter) to scope it down to its
essence. For instance, in a code generation application, a "templates" module
could be responsible for providing the global `*template.Template` which is used
to start the rendering.

Modules can depend on each other, and are allowed to form a directed acyclic
graph (DAG). Conceptually, a module will depend on another if it uses the
functionality of the child module in order to provide its functionality. In this
case, it is common for all dependencies on the child to go through the parent,
i.e. the parent provides an abstraction layer over the child. Continuing the
example above, a "generator" module which emits code to a `io.Writer` provided
an intermediate representation, would depend on the "templates" module describe
above.

An application startup process starts at a single root module, often called the
"app" module, then instantiates and starts all dependent module in reverse
depth-first traversal order. For instance, with the very simple module graph:

```
┌───────────┐   ┌───────────┐   ┌───────────┐
│ app       │───▷ generator │───▷ templates │
└───────────┘   └───────────┘   └───────────┘
```

The application startup would first start the "templates" module, then the
"generator" module, and lastly the "app" module.

### Defining modules

Concretely, to define a module, create a struct type:

```go
type app struct {
    ...
}
```

The module itself does not need to be exported, though package structure will
often push you to export it.

Each dependency that this module has on other modules is represented as a member
annotated with the `` `inject:""` `` tag:

```go
type app struct {
    Generator *codegen.Generator `inject:""`
}
```

When a dependency is listed, this instructs the `inject` library to ensure the
child modules are instantiated, and started before the parent module is itself
started. (And vice-versa with stopping, where the parent will be stopped before
the child is stopped.)

A module can request to be started by implementing the `inject.Start` interface,
or request to be stopped by implementing the `inject.Stop` interface.

### References

The `inject` library is a [dependency injection
framework](https://en.wikipedia.org/wiki/Dependency_injection). The most widely
used framework is [Guice](https://github.com/google/guice). This library solves
a much smaller problem, that of starting and stopping applications, not of
supporting dynamic injection at runtime. Such dynamic injection brings about
much complexity: concurrency model, lifetimes, scoping. All of that is
purposefully out of scope.

This library is heavily influenced by Facebook's
[inject](https://github.com/facebookarchive/inject) and
[startstop](https://github.com/facebookarchive/startstop).

## Develop

Configure

    fx set core.x64 --with //src/lib/inject:tests

And run the tests

    fx test inject_test
