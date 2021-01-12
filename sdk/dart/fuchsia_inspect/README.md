# Dart Inspect Library

Reviewed on: 2019-07-25

Inspect is a library for exposing structured trees of a component's internal
state. More can be read about inspect [here][inspect].

## Using

This library can be added to projects by including the
`//topaz/public/dart/fuchsia_inspect` GN target and importing the
`package:fuchsia_inspect/inspect.dart` package. Inspect is also available in the
SDK.

Documentation on the Inspect API is available [here][inspect_api].

Inspect data may be discovered and retrieved with [iquery][iquery].

## Testing

Unit tests for Inspect are available in the `fuchsia_inspect_package_unittests`
package. This includes tests from the `test/inspect` and `test/vmo` directories.
To run these tests, include `--with //topaz:tests` in the `fx set`
invocation and run the following command:

```
$ fx build && fx run-host-tests fuchsia_inspect_package_unittests
```

Integration tests are available in the `test/inspect_flutter_integration_tester`
and `test/integration` directories. The former has more documentation
[here][flutter_integration]. The latter may be run with the `fx set` invocation
described above and the following command:

```
$ fx run-test dart_inspect_vmo_test
```

## Source layout

Public members of the library are exposed in `lib/inspect.dart`. Private
implementations are in `lib/src/*`. Unit and integration tests may be found in
`test/*`.

[inspect]:/docs/development/inspect/README.md
[inspect_api]:https://fuchsia-docs.firebaseapp.com/dart/package-fuchsia_inspect_inspect/Inspect-class.html
[iquery]:/docs/development/inspect/iquery.md
[flutter_integration]:test/inspect_flutter_integration_tester/README.md
