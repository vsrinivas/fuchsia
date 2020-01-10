# ELF Runner

The ELF runner is the runner responsible for launching
[components][glossary-components] based on standard executable files (ELF
format).

A capability to the ELF runner service is offered to the root component under
the name `elf`. For other components to use the ELF runner, the capability must
be explicitly [routed][capability-routing] to them.

For a detailed explanation of how processes are created, please see
[`//docs/concepts/booting/program_loading.md`][program-loading].

## Using the ELF Runner

To use the ELF runner, the component must:

-   Add a [`use`][use] declaration for the ELF runner.
-   Add a `program` block, containing the binary and (optionally) program
    arguments that should be used.

The ELF runner receives instructions from the `program` section of the
[component manifest][glossary-component-manifests]. The `binary` field holds the
path to an executable file in the package the manifest comes from, and the
`args` field holds any additional string arguments that should be provided to
the process when it is created.

This is an example manifest that launches `bin/echo` with the arguments `Hello`
and `world!`. It assumes that the ELF runner capability has been offered to the
component under the name `elf`:

```cml
{
    "program": {
        "binary": "bin/echo",
        "args": [ "Hello", "world!" ],
    }
    "use": [
        { "runner": "elf" },
    ],
}
```

[use]: /docs/glossary.md#use
[capability-routing]: component_manifests.md#capability-routing
[glossary-components]: /docs/glossary.md#component
[program-loading]: /docs/concepts/booting/program_loading.md

<!-- TODO: the component manifest link describes v1 manifests -->
[glossary-component-manifests]: /docs/glossary.md#component-manifest
