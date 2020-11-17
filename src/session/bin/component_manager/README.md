# Component Manager for Session Framework

Reviewed on: 2020-10-20

This configuration of [Component Manager](/src/component/component_manager/README.md) allows command line tools to connect to services exposed by [`session_manager`](/src/session/bin/session_manager/README.md) at runtime. The specific configuration is set in `config.json`.

> TODO(fxbug.dev/45361): Remove this work-around entirely.
>
> See below for details.

## Building, Running, and Testing

Since this configuration is exclusively run in the context of using [`session_manager`](/src/session/bin/session_manager/README.md), refer to the instructions there.

## Details

It is currently impossible for a command line tool to acquire a service directly from a v2 component. This workaround uses a custom configuration of Component Manager to bridge service brokerage between the world of v1 components and v2 components.

This instance of Component Manager exposes services from its child's `exec/expose/svc` outgoing directory in *its* `out/svc` directory.

Used in combination with `sysmgr`'s knowledge of components' `out/svc` directories, this component can be used to allow command line tools to ask for specific services and have them provided by an instance of `session_manager`.

For example, it provides [session_manager.config](/src/session/bin/session_manager/meta/session_manager.config) to `sysmgr` and allows [session_control](/src/session/tools/session_control/README.md) to connect to the `fuchsia.session.Launcher` service through `session_control`'s environment.
