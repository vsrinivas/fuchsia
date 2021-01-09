# symbol-index: Manipulate symbol-index file

`symbol-index` is a host tool that manipulates the `symbol-index` file, which is
typically located at `~/.fuchsia/debug/symbol-index`. This file stores the
locations of debug symbols from multiple source code checkouts on the local
machine, thus debugging tools can read symbols across different checkouts.

## File format

A `symbol-index` could contain multiple lines. Each line could contain one or
two paths, separated by "\t". The first path points to the debug symbols, either
in `ids.txt` format or `.build-id` directory format. The optional second path
points to the build directory, which is used by debugging tools to lookup source
code.

For example, a Fuchsia checkout located at `/home/me/fuchsia` could have the
following content in the file.

```
/home/me/fuchsia/out/default/.build-id    /home/me/fuchsia/out/default
/home/me/fuchsia/prebuilt/.build-id
/home/me/fuchsia/prebuilt/third_party/clang/mac-x64/lib/debug/.build-id
```
