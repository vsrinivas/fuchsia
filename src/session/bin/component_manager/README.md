# Component Manager for Session Framework

Reviewed on: 2021-01-21

Component Manager for Session Framework is a custom instance of [Component
Manager][component-manager] that exposes services from
[`session_manager`][session-manager] to make them available
to command line tools.

It is currently impossible for a command line tool to acquire a service directly
from a v2 component. This workaround uses a custom configuration of Component
Manager to bridge service brokerage between the world of v1 components and v2
components.

This instance of Component Manager exposes services from its child's
`exec/expose/svc` outgoing directory in Component Manager's `out/svc` directory.
The services are made available to command line tools by supplying the
[`session_manager.config`][session-manager-config] configuration to `sysmgr`.

## Building

To add this project to your build, append `--with
//src/session/bin/component_manager:component_manager_sfw` to the `fx set`
invocation.

## Running

To launch Component Manager for Session Framework, run:

```
$ fx shell run fuchsia-pkg://fuchsia.com/component_manager_sfw#meta/component_manager_sfw.cmx <root_component_url>
```

Where `<root_component_url>` is the component URL for the component to launch
at the root of the v2 tree. Typically, this is the `session_manager` component
URL. For more information, see the documentation for
[`session_manager`][session-manager].

## Testing

For information on testing Component Manager, see the documentation for
[Component Manager][component-manager].

## Source layout

The source code for Component Manager is located in
[`//src/sys/component_manager`][component-manager-root]. The Session
Framework-specific configuration is in `config.json`.

[component-manager-root]: /src/sys/component_manager
[component-manager]: /src/sys/component_manager/README.md
[core-cml]: /src/sys/core/meta/core.cml
[session-manager-config]: /src/session/bin/session_manager/meta/session_manager.config
[session-manager]: /src/session/bin/session_manager/README.md
