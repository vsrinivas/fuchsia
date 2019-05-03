# ELF Runner

The ELF runner is the runner responsible for launching
[components][glossary-components] based on standard executable files (ELF
format).

For a detailed explanation of how processes are created, please see
[`//zircon/docs/program_loading.md`][program-loading].

## Using the ELF Runner

The ELF runner receives instructions from the `program` section of the
[component manifest][glossary-component-manifests]. The `binary` field holds the
path to an executable file in the package the manifest comes from, and the
`args` field holds any additional string arguments that should be provided to
the process when it is created.

This is an example manifest that launches `bin/echo` with the arguments `Hello`
and `world!`:

```cml
{
    "program": {
        "binary": "bin/echo",
        "args": [ "Hello", "world!" ],
    }
}
```

[glossary-components]: ../../glossary.md#component
[program-loading]: ../../../zircon/docs/program_loading.md
<!-- TODO: the component manifest link describes v1 manifests -->
[glossary-component-manifests]: ../../glossary.md#component-manifest
