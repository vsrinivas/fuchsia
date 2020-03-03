# ${PROJECT_NAME}

Reviewed on: YYYY-MM-DD

${PROJECT_NAME} is a library for validating Foo. Docs are available [here].

## Building

To add this project to your build, append `--with //src/sys/…` to the `fx
set` invocation.

## Using

${PROJECT_NAME} can be used by depending on the `//src/sys/lib/${PROJECT_NAME}`
GN target.

${PROJECT_NAME} is (not) available in the SDK.

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

The main implementation is in `src/lib.rc`, which also includes unit tests.
Helper functions for filesystem operations are located in `src/io.rs`.
