# intl_property_manager

Demo implementation of `fuchsia.intl.PropertyProvider`. This service provides a
`fuchsia.intl.Profile` that is configured using the protocol
`fuchsia.examples.intl.PropertyManager`.

`PropertyManager` is intended solely for demo purposes; real implementations of
`PropertyProvider` are expected to construct a `Profile` by reading the user's
settings.

Test the implementation using

```shell
$ fx run-test intl_property_manager_tests
```
