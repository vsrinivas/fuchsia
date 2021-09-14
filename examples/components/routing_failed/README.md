# Failed routing Example

This directory contains an example of failed [capability
routing][capability-routing] in [Component Framework][cf-intro] ([Components
v2][cfv2]).

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples
$ fx build
```

(Disclaimer: if these build rules become out-of-date, please check the
[Build documentation](docs/development/workflows) and update this README!)

## Running

Use `ffx component create` to create the component instances inside a restricted
realm for development purposes:

```bash
$ ffx component create /core/ffx-laboratory:echo_realm fuchsia-pkg://fuchsia.com/components-routing-failed-example#meta/echo_realm.cm
```

Start the client component instance by passing its moniker to
`ffx component bind`:

```bash
$ ffx component bind /core/ffx-laboratory:echo_realm/echo_client
```

To see component manager's log of the failed capability routing, run:

```bash
$ fx log
```

[capability-routing]: /docs/concepts/components/v2/component_manifests.md#capability-routing
[cf-intro]: /docs/concepts/components/v2/introduction.md
[cfv2]: /docs/glossary.md#components-v2
