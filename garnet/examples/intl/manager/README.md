# intl_property_manager

Demo implementation of [`fuchsia.intl.PropertyProvider`][1]. This service provides a
[`fuchsia.intl.Profile`][2] that is configured using the protocol
[`fuchsia.examples.intl.PropertyManager`][3].

`PropertyManager` is intended solely for demo purposes; real implementations of
`PropertyProvider` are expected to construct a `Profile` by reading the user's
settings.

Configure the environment using:

```
fx set core.x64 --with=//garnet/packages/tests:intl_examples
fx build
```

You can then start the QEMU with networking, assuming that you use QEMU as a testing device and
that you have confirmed that it works:

```
fx run -m 4096 -N
```

Once it runs:

```
fx serve
```

If all of the above are running, you can now test the implementation using:

```shell
$ fx run-test intl_property_manager_tests
```

[1]: https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.intl/property_provider.fidl
[2]: https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.intl/intl.fidl#69
[3]: https://fuchsia.googlesource.com/fuchsia/+/master/garnet/examples/intl/manager/fidl/manager.test.fidl