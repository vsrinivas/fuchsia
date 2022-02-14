# Other common situations

This section provides guidance on migrating components that use other common
capabilities or features.

## Resolvers

If your component is not part of the `base` package set for your product, you
must route the `universe` resolver to it. Resolvers are routed to components
using environments, and `core.cml` has a shared environment named
`full-resolver-env` for components outside of `base`.

Use the `list-packages` command to report the package sets where your component
package is included.

```posix-terminal
fx list-packages --verbose {{ '<var label="package name">my-package</var>' }}
```

If the package is not listed with the `base` tag, follow the remaining
instructions in this section.

When [adding your component][migrate-components-add], assign the shared
`full-resolver-env` as your component's `environment`.

```json5
// core.cml / component.core_shard.cml
{
  children: [
    ...
    {
      name: "my_component",
      url: "fuchsia-pkg://fuchsia.com/my-pkg#meta/my_component.cm",
      {{ '<strong>' }}environment: "#full-resolver-env",{{ '</strong>' }}
    },
  ],
}
```

## Start on boot {#start-on-boot}

Note: Starting component on boot is an area of active development. It is highly
recommended that you reach out to [component-framework-dev][cf-dev-list] before
migrating this behavior.

If your component appears in a sysmgr config `startup_services` or `apps` block
you should make your component an `eager` component in its parent's manifest.

```json5
// parent.cml
{
    children: [
        ...
        {
            name: "font_provider",
            url: "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cm",
            startup: "eager",
        },
    ],
}
```

Additionally you need to ensure that all your component's ancestors up to
`/core` are `eager` components. If your component is present on *all* products
that derive from the `core` you can [add it to core directly][migrate-add-core],
otherwise you need to use [core realm variability][migrate-add-shard] to allow
products without your component to continue to boot.

The `eager` component should be in the base package set; `eager` is generally
incompatible with being outside the base package set.

For more details on how `eager` impacts component startup see,
[lifecycle][eager-lifecycle] and [component manifests][eager-manifest].

## Critical components {#critical-components}

[`critical_components`][sysmgr-critical-components] is a sysmgr feature that
allows a component to mark itself as critical to system operation:

```json
{
  ...
  "critical_components": [
    "fuchsia-pkg://fuchsia.com/system-update-checker#meta/system-update-checker.cmx"
  ]
}
```

The equivalent feature in Components v2 is called "reboot-on-terminate". If your
component appears in `critical_components` you should mark it as `on_terminate:
reboot` in the parent component's manifest:

```
// core.cml / component.core_shard.cml
{
    children: [
        ...
        {
            name: "system-update-checker",
            url: "fuchsia-pkg://fuchsia.com/system-update-checker#meta/system-update-checker.cm",
            startup: "eager",
            on_terminate: "reboot",
        },
    ],
}
```

Also, you'll need to add the component's moniker to component manager's security
policy allowlist at
[`//src/security/policy/component_manager_policy.json5`][src-security-policy]:

```
// //src/security/policy/component_manager_policy.json5
{
    security_policy: {
        ...
        child_policy: {
            reboot_on_terminate: [
                ...
                "/core/system-update-checker",
            ],
        },
    },
}
```

## Shell binaries

Your project may contain a `fuchsia_shell_package()` build target designed to
execute in a shell environment. Many of these packages also contain a CMX file
to support invoking the binary as a v1 component. When
[routing your services][migrate-components-v1] to the `sys` environment,
include any services required by shell binaries.

Note: If your component requires `shell-commands` directory access to invoke
shell binaries, see [directory features][migrate-features-directory] for more
details.

Shell binaries are run in the `sys` [environment][glossary.environment], and
have access to all the capabilities provided there. Capabilities are not defined
by the CMX manifest file unless shell binaries are invoked as a component using
the `run` command.

When working with shell binaries, consider the following:

-   If you only need access to the binary through a shell interface, remove the
    unused CMX file entirely. Do not replace it with a corresponding CML file.
-   If you need to access the binary from somewhere else in the v2 component
    topology (such as tests), migrate the functionality into a new v2 component
    instead.

Note: There is no v2 equivalent of using `run` to invoke a shell binary **as a
component**. If you require this feature for your component, reach out to
[component-framework-dev][cf-dev-list].

## Lifecycle

If your component serves the `fuchsia.process.lifecycle.Lifecycle` protocol,
follow these instructions to migrate to the lifecycle handle provided by the
ELF runner in Components v2.

1.  Remove your component's entry in the `appmgr`
    [allowlist][cs-appmgr-allowlist]:

    ```cpp
    // Remove this entry.
    lifecycle_allowlist.insert(component::Moniker{
        .url = "fuchsia-pkg://fuchsia.com/my_package#meta/my_component.cmx", .realm_path = {"app", "sys"}});
    ```
1.  When [migrating your component manifest][migrate-components], add
    the lifecycle stop event:

    ```json5
    // my_component.cml
    {
        include: [
            "syslog/client.shard.cml",
        ],
        program: {
            runner: "elf",
            binary: "bin/my_binary",
            {{ '<strong>' }}lifecycle: { stop_event: "notify" },{{ '</strong>' }}
        },
        ...
    }
    ```

1. Get the `fuchsia.process.lifecycle.Lifecycle` channel provided by the ELF
   runner:

    * {Rust}

      ```rust
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/lifecycle/rust/src/main.rs" region_tag="imports" adjust_indentation="auto" %}
      // ...
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/lifecycle/rust/src/main.rs" region_tag="lifecycle_handler" adjust_indentation="auto" %}
      ```

    * {C++}

      ```cpp
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/lifecycle/cpp/main.cc" region_tag="imports" adjust_indentation="auto" %}
      // ...
      {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/components/lifecycle/cpp/main.cc" region_tag="lifecycle_handler" adjust_indentation="auto" %}
      ```

Note: For a complete lifecycle example, see
[`//examples/components/lifecycle`][lifecycle-example].

More information about the Lifecycle protocol is available in the
[ELF runner documentation][elf-runner-docs].

## What's next {#next}

Explore the following sections for additional migration guidance on
specific features your components may support:

-   [Component sandbox features](features.md)
-   [Diagnostics capabilities](diagnostics.md)

[cf-dev-list]: https://groups.google.com/a/fuchsia.dev/g/component-framework-dev
[cs-appmgr-allowlist]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/sys/appmgr/main.cc;l=125;drc=ddf6d10ce8cf63268e21620638ea02e9b2b7cd20
[eager-lifecycle]: /docs/concepts/components/v2/lifecycle.md#eager
[eager-manifest]: https://fuchsia.dev/reference/cml#children
[elf-runner-docs]: /docs/concepts/components/v2/elf_runner.md#lifecycle
[glossary.environment]: /docs/glossary/README.md#environment
[migrate-add-core]: /docs/development/components/v2/migration/components.md#add-core-direct
[migrate-add-shard]: /docs/development/components/v2/migration/components.md#add-core-shard
[migrate-components]: /docs/development/components/v2/migration/components.md
[migrate-components-add]: /docs/development/components/v2/migration/components.md#add-component-to-topology
[migrate-components-v1]: /docs/development/components/v2/migration/components.md#route-to-v1
[migrate-features-directory]: /docs/development/components/v2/migration/features.md#directory-features
[lifecycle-example]: /examples/components/lifecycle
[src-security-policy]: /src/security/policy/component_manager_policy.json5
[sysmgr-critical-components]: /docs/concepts/components/v1/sysmgr.md#critical_components