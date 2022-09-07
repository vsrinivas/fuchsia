# intl-services-system-test

This is a smoke test for the `fuchsia.intl.PropertyProvider` protocol
implementer (usually `intl`). It also relies on `setui_service` and
that component's implementation of `fuchsia.settings.Intl`.

This test modifies the user's internationalization preferences but attempts to
restore the original values after it runs.

## Execution

```shell
fx set core.x64 --with //sdk/ctf/tests/fidl/fuchsia.intl:tests
# ...
fx test intl-services-system-test
```
