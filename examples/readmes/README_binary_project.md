# ${PROJECT_NAME}

Reviewed on: YYYY-MM-DD

${PROJECT_NAME} is the part of the system that drives Foo. More can be read
about it [here].

## Building

To add this project to your build, append `--with //src/sys/…` to the `fx
set` invocation.

## Running

${PROJECT_NAME} provides the `fuchsia.foo.Bar` service on Fuchsia, and can be
controlled via the `${PROJECT_NAME}_ctl` command:

```
$ fx shell run ${PROJECT_NAME}_ctl --help
```

## Testing

Unit tests for ${PROJECT_NAME} are available in the `${PROJECT_NAME}_tests`
package.

Integration tests are also available in the `${PROJECT_NAME}_integration_tests`
package.

```
$ fx run-test ${PROJECT_NAME}_tests
$ fx run-test ${PROJECT_NAME}_integration_tests
```

## Source layout

The entrypoint is located in `src/main.rs`, and the core model implementation is
under `src/model/`. Unit tests are co-located with the code, with the exception
of `src/model/` which has unit tests in `src/model/tests/`. Integration tests
live in `tests/`.
