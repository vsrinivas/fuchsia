Fuchsia Boot Sequence
=====================

This document describes the boot sequence for Fuchsia from the time the Magenta
layer hands control over to the Fuchsia layer.  This document is a work in
progress that will need to be extended as we bring up more of the system

# Layer 1:
[appmgr](https://fuchsia.googlesource.com/application/+/master/src/manager)

`appmgr`'s job is to host the environment tree and help create
processes in these environments.  Processes created by `appmgr`
have an `mx::channel` back to their environment, which lets them create other
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

# Layer 3: ... to be continued

(In the future, the boot sequence will likely continue from this point into the
device runner, user runner, user shell, etc.)
