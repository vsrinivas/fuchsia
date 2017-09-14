Fuchsia Boot Sequence
=====================

This document describes the boot sequence for Fuchsia from the time the Zircon
layer hands control over to the Fuchsia layer.  This document is a work in
progress that will need to be extended as we bring up more of the system

# Layer 1: [appmgr](https://fuchsia.googlesource.com/application/+/master/src/manager)

`appmgr`'s job is to host the environment tree and help create
processes in these environments.  Processes created by `appmgr`
have an `zx::channel` back to their environment, which lets them create other
processes in their environment and to create nested environments.

At startup, `appmgr` creates an empty root environment and creates
the initial apps listed in `/system/data/appmgr/initial.config` in
that environment. Typically, these applications create environments nested
directly in the root environment. The default configuration contains one initial
app: `bootstrap`.

# Layer 2: [bootstrap](https://fuchsia.googlesource.com/modular/+/master/src/bootstrap/)

`bootstrap`'s job is to create the boot environment and create a number of
 initial applications in the boot environment.

The services that `bootstrap` offers in the boot environment are not provided by
bootstrap itself. Instead, when bootstrap receives a request for a service for
the first time, `bootstrap` lazily creates the appropriate app to implement that
service and routes the request to that app. The table of which applications
implement which services is contained in the
`/system/data/bootstrap/services.config` file. Subsequent requests for the same
service are routed to the already running app. If the app terminates,
`bootstrap` will start it again the next time it receives a request for a
service implemented by that app.

`bootstrap` also runs a number of application in the boot environment at
startup. The list of applications to run at startup is contained in the
`/system/data/bootstrap/apps.config` file. By default, this list includes
`/boot/bin/sh /system/autorun`, which is a useful development hook for the
boot sequence, and `run-vc`, which creates virtual consoles.

# Layer 3: [device_runner](https://fuchsia.googlesource.com/modular/+/master/src/device_runner/)

`device_runner`'s job is to setup interactive flow, user login and user
management.

It first gets access to the root view of the system, starts up Device Shell and
draws the Device Shell UI in the root view starting the interative flow. It also
manages a user database that is exposed to Device Shell via a FIDL API.

This API allows the Device Shell to add a new user, delete an existing user,
enumerate all existing users and login as an existing user or in incognito mode.

Adding a new user is done using an Account Manager service that can talk to an
identity provider to get an id token to access the user's
[Ledger](https://fuchsia.googlesource.com/ledger/).

Logging-in as an existing user starts an instance of `user_runner` with that
user's id token and with a namespace that is mapped within and managed by
`device_runner`'s namespace.

Logging-in as a guest user (in incognito mode) starts an instance of
`user_runner` but without an id token and a temporary namespace.

# Layer 4: ... to be continued

(In the future, the boot sequence will likely continue from this point into the
user runner, user shell, etc.)
