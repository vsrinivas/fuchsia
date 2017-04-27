# Namespaces

## What is a namespace?

A namespace is the composite hierarchy of files, directories, sockets, services,
and other named objects which are offered to application components by their
environment.

The location of each object within a namespace can be described by delimited
path expressions of the form `a/b/c`.

## So a namespace is like a root filesystem?

Sort of but it's not universal; it's subjective.

Not all components perceive the same namespace. The contents and structure of
the namespace provided to each component varies depending on the component's
nature, identity, scope, relation to other components, and rights.

In fact, different instances of the same component may perceive quite different
namespaces according to their respective roles.

## What's inside a namespace?

A namespace may contain many kinds of objects such as:

 - read-only executables and assets
 - local persistent storage
 - temporary storage
 - services offered by the system, the application framework, or the embedder
 - device nodes (for drivers and privileged components)

## How is a namespace composed?

When a component is instantiated in an environment (eg. its process is started),
it receives a collection of paths and object handles representing the structure
of its namespace. The paths themselves encode the intended significance of the
objects within the namespace by convention.

## What does a namespace look like?

Here's an illustration of the conventional namespace organization for a typical
component. Some kinds of components (serving specialized purposes) may receive
additional paths not described here. Other kinds of components (in more
restrictive sandboxes) may receive fewer.

_The structure of the namespace which a component can expect to see should be
described as part of the documentation for how one would go about implementing
that particular kind of component._

 - `pkg/`: binaries and assets (read-only)
    - `my-program/`
        - `bin/`
        - `lib/`
        - `assets/`
    - `a-package-my-program-depends-on/`
        - `bin/`, `lib/`, `assets/`, ...
 - `data/`: local persistent storage (read-write)
    - `my-file.txt`
 - `tmp/`: temporary storage (read-write)
 - `svc/`: services offered to the component
    - `fuchsia.com/`
        - `compositor/`: create graphical objects
        - `container/`: create sub-environments and sub-components
        - `fonts/`: font provider
        - `http-client/`: http client
        - `http-server/`: http server
        - `icu/`: ICU data provider
        - `ledger/`: replicated storage
        - `log/`: logging service
        - `mailbox/`: the component’s default message queue
        - `media/`: media server
        - `messaging/`: cross-device messaging
        - `mdns/`: MDNS lookup
        - `network/`: TCP/IP stack
        - `resolver/`: query the component index and retrieve manifests
        - `story/`: interact with the containing story (modules only)
        - `time/`: query or manipulate the real-time clock
        - `trace-controller/`: start/stop performance tracing
        - ... and many many more things...
    - `vendor.com/`
        - `fancy-display/`
        - `fancy-pen/`
    - `dev/`: device tree (relevant portions visible to privileged components as needed)
        - `class/`, ...
    - `env/`: introspect the structure of the environment (privileged components only)
        - `boot/`: information about the "boot" environment
            - `1234/`: information about component instance #1234
                - `name/`: component name
                - `origin/`: component origin url
                - `local/`: namespace seen by the component itself
                    - `bin/`, `data/`, `svc/`, ...
                - `pub/`: namespace published by the component
                    - `svc/`, ...
            - `device/`: information about the "device" environment
                - `user:aparna/`: information about Aparna's user environment
                    - `4567/`: information about component instance #4567
                    - `origin/`, `ns/`, `pub/`, ...

## What's a service anyway?

A service is a well-known object which can be discovered by name and which
provides an implementation of a FIDL interface that performs a particular role
in the system.

For example, the service object located at `/svc/fuchsia.com/log` provides an
implementation of the logging interface which components should use by default.
In this case, the service’s name is `fuchsia.com/log`.

There may be other implementations of the logging interface available using
different service names. For example, the service object located at
`/svc/vendor.com/fancy-log` could offer a fancier logging service which happens
to implement the same interface. Clients which are aware of the distinction may
choose to ask for this more specialized logging service when available instead
of the default one.

## How are services configured?

The environment's namespace specifies how service names are bound to specific
implementations. Different environments may bind different implementations to
the same service names.

It may be helpful to think of the environment’s role in service resolution as a
function which:

 1. accepts the name of a service which the clients wants to access
 2. resolves the service name to the location of a component which offers that service
 3. activates (starts or finds an existing instance of) that component
 4. returns a reference to the appropriate service published by the component

For resolution, a simple environment might use a configuration file to map
service names to components:

 - `fuchsia.com/compositor` ⇒ `http://fuchsia.com/mozart/compositor.far`
 - `fuchsia.com/log` ⇒ `http://vendor.com/fancy-services/fancy-logger.far`

Notice that `vendor.com` can implement an interface defined by `fuchsia.com`.

More complex environments might leverage indexes of components, user
preferences, or administrative policies.
