# The CTF Test Realm

[CTF] tests run in a special realm which is meant to isolate any capabilities
that are required only for CTF testing. Tests must opt-in to the realm by
declaring a facet in their component manifests, as shown below.

```json5
// my_test.cml

{
    include: [
        {{ '<strong>' }}"//sdk/ctf/test_realm/meta/cts.shard.cml",{{ '</strong>' }}
        "//src/sys/test_runners/rust/default.shard.cml",
    ],
    program: {
        binary: "bin/my_test_binary",
    },
}
```

Below is the list of capabilities provided to CTF tests:
{# Update the list when modifying //sdk/ctf/test_realm/meta/cts_test_realm.shard.cml #}

Protocols:

```text
fuchsia.hwinfo.Board
fuchsia.hwinfo.Device
fuchsia.hwinfo.Product
fuchsia.logger.LogSink
fuchsia.process.Launcher
fuchsia.process.Resolver
fuchsia.settings.Privacy
fuchsia.sys2.EventSource
```

Storage:

```text
data
tmp
cache
custom_artifacts
```

[CTF]: /docs/development/testing/ctf/overview.md
