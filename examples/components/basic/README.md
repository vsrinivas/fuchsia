# Basic Components

This directory contains simple examples of components in [Component
Framework](docs/concepts/components/introduction.md)
([Components v2](docs/glossary.md#components-v2)).

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples --with //examples:tests
$ fx build
```

(Disclaimer: if these build rules become out-of-date, please check the
[Build documentation](docs/development/workflows) and update this README!)

## Running

Use the `locate` tool to find the fuchsia-pkg URLs of these components:

```bash
$ fx shell locate hello_world
```

Pick the URL of a component, and provide it to `run` as an argument to
`component_manager`:

```bash
$ fx shell 'run fuchsia-pkg://fuchsia.com/component_manager#meta/component_manager.cmx fuchsia-pkg://fuchsia.com/components_basic_example#meta/hello_world.cm'
```

This will run the component in an instance of component manager as a v1
component.

Make sure you have `fx serve` running in another terminal so your component can
be installed!

## Testing

To run one of the test components defined here, use `fx test` with the
package name:

```bash
$ fx test hello-world-tests
```
