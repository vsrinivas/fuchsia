# Routing Example

This directory contains an example of [capability
routing](docs/concepts/components/component_manifests#capability-routing) in [Component
Framework](docs/concepts/components/introduction.md)
([Components v2](docs/glossary.md#components-v2)).

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
$ ffx component run fuchsia-pkg://fuchsia.com/components-routing-example#meta/echo_realm.cm
```

Make sure you have `fx serve` running in another terminal so your component can
be installed!
