# Update tool end-to-end test

## What is this test?

This test verifies that the `update` tool can communicate with the
implementation for [//sdk/fidl/fuchsia.update/update.fidl](update_fidl).

## Fuchsia Components involved

Depending on the product configuration, this uses one of the following
components which implement the above-mentioned FIDL procotol.

### omaha-client

The [//src/sys/pkg/bin/omaha-client](omaha-client-service) implements the
client end of the Omaha protocol, and provides signalling for the
availability of updates for the system.

### system-update-checker

The [//src/sys/pkg/bin/system-update-checker](system-update-checker)
checks for updated system software by looking in the TUF repositories
for the `fuchisa-pkg://fuchsia.com/update` package.

## Other Tests

Integration and unit tests for this functionality are:
- [//src/sys/pkg/bin/omaha-client](omaha-client-service)
- [//src/sys/pkg/lib/omaha-client](omaha-client)
- [//src/sys/pkg/bin/system-update-checker](system-update-checker)
- [//src/sys/pkg/tests/system-update-checker](system-update-checker-tests)

[update_fidl]: https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.update/update.fidl
[omaha-client-service]: https://fuchsia.googlesource.com/fuchsia/+/master/src/sys/pkg/bin/omaha-client
[omaha-client]: https://fuchsia.googlesource.com/fuchsia/+/master/src/sys/pkg/lib/omaha-client
[system-update-checker]: https://fuchsia.googlesource.com/fuchsia/+/master/src/sys/pkg/bin/system-update-checker
[system-update-checker-tests]: https://fuchsia.googlesource.com/fuchsia/+/master/src/sys/pkg/tests/system-update-checker

