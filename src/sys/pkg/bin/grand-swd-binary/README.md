# Grand SWD Binary

This binary aggregates the `pkg-resolver` and (coming soon) the `ota-dependency-checker` into a
single binary to save disk space.  The Grand SWD Binary inspects `argv[0]` on start and invokes
the appropriate binary based on the binary's name.

This is based off //src/sys/pkg/bin/multi-universal-tool, where we do the same thing with
pkgctl and the update tool.
