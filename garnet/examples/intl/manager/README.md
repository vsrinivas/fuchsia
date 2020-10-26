# intl_property_manager

Demo implementation of [`fuchsia.intl.PropertyProvider`][1]. This service provides a
[`fuchsia.intl.Profile`][2] that is configured using the protocol
[`fuchsia.examples.intl.PropertyManager`][3].

`PropertyManager` is intended solely for demo purposes; real implementations of
`PropertyProvider` are expected to construct a `Profile` by reading the user's
settings.

## Initial profile

Without additional flags, the `intl_property_manager` serves an empty `Profile`.  It is
possible to add flags to instruct it to serve a nonempty `Profile`, as follows:

* `--set_initial_profile`: this flag *must* be set to instruct the server to serve a nonempty
initial locale.
* `--locale_ids=...`: a comma-separated list of BCP-47 compatible locale identifiers to be served,
in the order of preference.
* `--locale_ids=...`: a comma-separated list of BCP-47 compatible time zone identifiers to be served, in the order of preference.

The above flags can be set in the `"args"` section of the file
[`intl_property_manager.cmx`](meta/intl_property_manager.cmx).

In case the above list of flags goes out of date, running `intl_property_manager` with the flag
`--help` will always display the currently accepted list of flags.

# Compile and run

Configure the environment using:

```
fx set core.x64 --with=//garnet/packages/tests:intl_examples
fx build
```

You can then start the QEMU with networking, assuming that you use QEMU as a testing device and
that you have confirmed that it works:

```
fx emu -N
```

Once it runs:

```
fx serve
```

If all of the above are running, you can now test the implementation using:

```shell
$ fx run-test intl_property_manager_tests
```

[1]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.intl/property_provider.fidl
[2]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.intl/intl.fidl#69
[3]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/garnet/examples/intl/manager/fidl/manager.test.fidl
