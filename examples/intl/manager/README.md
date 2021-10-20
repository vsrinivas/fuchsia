# intl_property_manager

Demo implementation of [`fuchsia.intl.PropertyProvider`][1]. This service provides a
[`fuchsia.intl.Profile`][2] that is configured using the protocol
[`fuchsia.examples.intl.PropertyManager`][3].

`PropertyManager` is intended solely for demo purposes; real implementations of
`PropertyProvider` are expected to construct a `Profile` by reading the user's
settings.

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples --with //examples:tests
$ fx build
```

If you do not already have one running, start a package server so the example
components can be resolved from your device:

```bash
$ fx serve
```

## Running

To run the example component, provide the full component URL to `run`:

```bash
$ ffx component run 'fuchsia-pkg://fuchsia.com/intl_property_manager#meta/intl_property_manager.cm'
```

You can see the component output using `fx log`.

## Testing

To run the test components defined here, provide the build target to
`fx test`:

```bash
$ fx test intl_property_manager_tests
```

## Options

Without additional flags, the `intl_property_manager` serves an empty `Profile`.
It is possible to add flags to instruct it to serve a nonempty `Profile`, as
follows:

* `--set_initial_profile`: this flag *must* be set to instruct the server to
  serve a nonempty initial locale.
* `--locale_ids=...`: a comma-separated list of BCP-47 compatible locale
  identifiers to be served, in the order of preference.
* `--locale_ids=...`: a comma-separated list of BCP-47 compatible time zone
  identifiers to be served, in the order of preference.

The above flags can be set in the `"args"` section of the file
[`intl_property_manager.cml`](meta/intl_property_manager.cml).

[1]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.intl/property_provider.fidl
[2]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.intl/intl.fidl#69
[3]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/examples/intl/manager/fidl/manager.test.fidl
