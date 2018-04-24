# common

This directory contains components and configuration that are used by multiple
integration tests.

Specifically, there are two modules: `DoneModule` calls `ModuleContext.Done()`
immediately when it gets initialized. `NullModule` doesn't do anything and just
sits there.

Both are deployed as their own packages and have a module manifest in their
package. They are added to the configuration of the `ModuleResolver` so they can
be started by an `Intent`.
