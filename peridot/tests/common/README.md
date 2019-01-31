# common

This directory contains components and configuration that are used by multiple
integration tests.

Specifically, there is a module `NullModule` which doesn't do anything and just
sits there.

It is deployed as its own package and has a module manifest in its package. It
is added to the configuration of the `fuchsia::modular::ModuleResolver` so it can be started by an
`fuchsia::modular::Intent`.
