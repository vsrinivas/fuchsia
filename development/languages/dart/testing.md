# Testing


The `dart_test` target is appropriate for unit tests.
Each target yields a test script in the output directory under:
```sh
out/<build-type>/gen/path/to/package/target_name.sh
```
This script simply runs the given tests in the Flutter shell *on the host*.

The `//scripts/run-dart-action.py` script may be used to run multiple test
suites at once:
```sh
scripts/run-dart-action.py test --out out/<build-type> --tree //topaz/shell/*
```
It also works with a single suite:
```sh
scripts/run-dart-action.py test --out out/<build-type> --tree //topaz/shell/armadillo:test
```
