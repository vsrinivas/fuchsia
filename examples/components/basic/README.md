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

These examples are all stored in a package named `components-basic-example`.

There isn't yet a convenient way to directly run a native v2 component, so these
examples are run by launching a new component manager in a v1 component to run
them. A component manager packaged to be a v1 component is also included in this
package for this purpose.

The component manager can be invoked with the `run` command, and given a URL for
which test component to launch. As an example, the following will run the
`hello_world` example.

```bash
$ fx shell 'run fuchsia-pkg://fuchsia.com/components-basic-example#meta/component_manager_for_examples.cmx fuchsia-pkg://fuchsia.com/components-basic-example#meta/hello-world.cm'
```

When the above command is run, the following output can be seen in `fx log`:

```
[682199.986470][5056597][5056599][hello_world] INFO: Hippo: Hello World!
```

To run a different example, replace `hello-world` with the name of a different
manifest in `meta/`.

Make sure you have `fx serve` running in another terminal so your component can
be installed!

## Testing

To run one of the test components defined here, use `fx test` with the
package name:

```bash
$ fx test hello-world-tests
```
