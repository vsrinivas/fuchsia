# `v2-argh-wrapper`

`v2-argh-wrapper` is a library to load command lines and log argh error output.

By default, argh sends its error output to stdout, but v2 components do not
support stdout. It takes 15 lines of boilerplate to use the non-default argh
entry point and log its output; hence this library.

`v2-argh-wrapper` provides a single function,
`pub fn load_command_line<T: argh::FromArgs>() -> Result<T, Error>`.

If argh parses the command line successfully, the function returns the
FromArgs struct it was parsed into. If the parse was unsuccessful or the
args asked for a usage string, `load_command_line()` will return an
anyhow::Error.

## Building

This project should be automatically included in builds that depend on it.

## Using

`v2-argh-wrapper` can be used by depending on the
`//src/diagnostics/lib/util/v2-argh-wrapper` gn target and then using
the `v2-argh-wrapper` crate in a rust project.

`v2-argh-wrapper` is not available in the sdk and is intended to be used only by
diagnostics binaries.

## Testing

This library will be integration-tested by every program that uses it.
No unit tests are currently contemplated.
