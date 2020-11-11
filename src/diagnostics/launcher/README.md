# `launcher`

`launcher` is a system to combine several programs into a single binary, to
save memory and disk space. Each program can be written in its own directory
and run independently.

The Fuchsia operating system will maintain one VMO containing the `launcher`
binary image, and will serve that VMO to each package which uses one of the
programs that were combined to make the `launcher` binary.

## Building

This project should be automatically included in builds by including any
project that uses it.

## Using

To convert a program to use `launcher`, you will build a library instead of a
binary; include the library in the `launcher` binary; link to the `launcher`
binary from the component that used to use your program's binary; and adjust
your .cml files slightly.

In your program's directory:

1. Rename its `main.rs` to `lib.rs`
1. From lib.rs export your `argh` command-line arg struct, for example
`pub struct CommandLine`. (If you don't use command line args, add an
empty struct with `#[derive(FromArgs, Debug, PartialEq)]`.)
    1. Make sure to derive PartialEq on your arg struct.
    1. Annotate your struct with `#[argh(subcommand, name = "your-choice")]`.
1. Also from lib.rs, export the former `main` funtion with this signature:
`pub async fn main(args: CommandLine) -> Result<(), Error>`
    1. Remove the `#[fasync::run_singlethreaded]` or similar lines.
1. In BUILD.gn, `import("//build/rust/rustc_library.gni")`, change
`rustc_bin` to `rustc_lib`, and replace `main.rs` with `lib.rs` in `sources`.
1. In the .cmx for unit tests, change `_bin_test` to `_lib_test`.

In `//src/diagnostics/launcher`:

1. Add your library to `deps` in BUILD.gn
1. Add your `CommandLine` struct to the `ChildArgs` enum in main.rs
1. Call your library's `main` from the `match` in main()

To invoke a program that has been integrated in `launcher`:

1. Make the component depend on `"//src/diagnostics/launcher:bin"` instead of
the original binary.
1. In the .cml use `program` -> `binary` -> "bin/launcher".
1. Add the appropriate command line argument (`your-choice` in this example) to
the .cml file as the first `args` item.

## Testing

`launcher` will be integration-tested by every program that uses it.
No unit tests are currently contemplated.
