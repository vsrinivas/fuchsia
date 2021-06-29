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

`ffx` can be used to run the `echo_realm` example:

```bash
$ ffx component run fuchsia-pkg://fuchsia.com/components-routing-failed-example#meta/echo_realm.cm
```

Make sure you have `fx serve` running in another terminal so your component can
be installed!

To see component manager's log of the failed capability routing, run:

```bash
$ fx log
```

[capability-routing]: /docs/concepts/components/v2/component_manifests.md#capability-routing
[cf-intro]: /docs/concepts/components/v2/introduction.md
[cfv2]: /docs/glossary.md#components-v2
