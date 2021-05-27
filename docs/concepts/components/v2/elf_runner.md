# ELF Runner

<<../_v2_banner.md>>

The ELF runner is the runner responsible for launching
[components][glossary-components] based on standard executable files (ELF
format).

A capability to the ELF runner service is offered to the root component under
the name `elf`. For other components to use the ELF runner, the capability must
be explicitly [routed][capability-routing] to them.

For a detailed explanation of how processes are created, please see
[`//docs/concepts/booting/program_loading.md`][program-loading].

## Using the ELF Runner

To use the ELF runner, the component must add a `program` block, containing:

- the binary
- the runner the program uses (unless that is already included via a [CML shard][cml-shards])
- (optionally) program arguments that should be used.

The ELF runner receives instructions from the `program` section of the
[component manifest][component-manifests]. The `binary` field holds the
path to an executable file in the package the manifest comes from, and the
`args` field holds any additional string arguments that should be provided to
the process when it is created.

The `main_process_critical` field may be used to mark the component's first
process as [critical to component manager's job][job-set-critical], which
causes component manager (and by extension, all components) to be terminated if
the process exits with a non-zero code. An
[allowlist][main-process-critical-allowlist] is used to control which
components may use this field.

This is an example manifest that launches `bin/echo` with the arguments `Hello`
and `world!`. It assumes that the ELF runner capability has been offered to the
component under the name `elf`:

```cml
{
    program: {
        runner: "elf",
        binary: "bin/echo",
        args: [ "Hello", "world!" ],
    }
}
```

### Lifecycle

Components have a [lifecycle][lifecycle]. Components run by the ELF runner can
integrate with the lifecycle if you add a `lifecycle` attribute to your
component manifest.

```cml
{
    program: {
        runner: "elf",
        binary: "bin/echo",
        lifecycle: { stop_event: "notify" },
    }
}

```

The program should take the handle to the Lifecycle channel and serve the
[Lifecycle protocol][lc-proto] on that channel. The component should exit after
receiving and processing the `stop` call. For an example see this
[sample code][lc-example].

The ELF Runner monitors the process it started for the program binary of the
component. If this process exits, the ELF runner will terminate the component's
execution context, which includes the component's job and all subprocesses.

### Forwarding stdout and stderr streams

The stdout and stderr streams of ELF components can be routed to the
[LogSink service][logsink]. By default, the ELF runner doesn't route these
streams to any output sink. Therefore, any write to these streams, such as `printf`,
is lost and can be considered a no-op. If your component prints diagnostics
messages to either of these streams, you should forward the streams to the
[LogSink service][logsink].


To enable this feature, add the following to your manifest file:

```json5
{
    include: [ "sdk/lib/diagnostics/syslog/elf_stdio.shard.cml" ],
    ...
}
```

After including this shard, all writes to stdout are logged as INFO messages,
and all writes to stderr are logged as WARN messages. Messages are split
by newlines and decoded as UTF-8 strings. Invalid byte sequences are converted
to the U+FFFD replacement character, which usually looks like `�`.

See also: [RFC-0069: Standard I/O in ELF Runner][rfc0069]

Note: There are known issues where messages from `ZX_ASSERT_...` in C/C++
components and `Error` objects returned in `main` in Rust components are lost.
For more information, see [fxb-72178] and [fxb-72764] respectively.

[capability-routing]: component_manifests.md#capability-routing
[cml-shards]: component_manifests.md#include
[component-manifests]: /docs/concepts/components/v2/component_manifests.md
[fxb-72178]: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=72178
[fxb-72764]: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=72764
[glossary-components]: /docs/glossary.md#component
[job-set-critical]: /docs/reference/syscalls/job_set_critical.md
[lc-example]: /examples/components/basic/src/lifecycle_full.rs
[lc-proto]: /sdk/fidl/fuchsia.process.lifecycle/lifecycle.fidl
[lifecycle]: lifecycle.md
[logsink]: /docs/development/diagnostics/logs/recording.md#logsinksyslog
[main-process-critical-allowlist]: /src/security/policy/component_manager_policy.json5
[program-loading]: /docs/concepts/booting/program_loading.md
[rfc0069]: /docs/contribute/governance/rfcs/0069_stdio_in_elf_runner.md
[use]: /docs/glossary.md#use
