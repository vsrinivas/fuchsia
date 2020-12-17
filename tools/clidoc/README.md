CliDoc is a command-line tool that generates documentation for core Fuchsia developer tools based on their --help output

# Build
1. Include clidoc by adding a direct or indirect reference to it in your `fx set` (e.g. `fx set core.x64 --with //tools/clidoc`)
1. Build with `fx build tools/clidoc:clidoc`

# Run
1. Run with `fx clidoc`

# WIP
This tool is a work in progress and currently being actively developed. Next steps are to generate a markdown file of the collected developer tool --help output, which will be the basis for the reference documentation on fuchsia.dev
