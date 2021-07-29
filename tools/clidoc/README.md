# Clidoc

CliDoc is a command-line tool that generates documentation for core Fuchsia developer tools based on their --help output

## Build

1. Include clidoc by adding a direct or indirect reference to it in your `fx set` (e.g. `fx set core.x64 --with //tools/clidoc`)
1. Build with `fx build tools/`

## Run

1. Run with `fx clidoc` or `fx clidoc <List of allowed commands>`
1. If you don't specify a list of commands, clidoc defaults to an internal allowlist of commands in //tools/clidoc/src/main.rs
1. When you specify a list of commands, absolute paths are used as-is and relative paths are on the input_path.

## Options

1. Run `fx clidoc --help` to see options, including setting the input and output directory.
1. The default input directory is the parent directory of this clidoc tool, which contains host tool executables. It is most likely //out/default/host_x64 or //out/default/host-tools.
1. The default output directory is whichever directory the user is in when executing the clidoc command.