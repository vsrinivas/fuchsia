# Basic Components

This directory contains simple examples of components in [Component
Framework](/docs/concepts/components/introduction.md)
([Components v2](/docs/glossary.md#components-v2)).

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples --with //examples:tests
$ fx build
```

(Disclaimer: if these build rules become out-of-date, please check the
[Build documentation](/docs/development/workflows) and update this README!)

## Running

These examples are all stored in a package named `components-basic-example`.

`ffx` can be used to run the `hello_world` example:

```bash
$ ffx component run fuchsia-pkg://fuchsia.com/components-basic-example#meta/hello-world.cm
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
