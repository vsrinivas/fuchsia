# Multi Universal Tool (MUT)

This binary aggregates the `pkgctl` and `update` commandline tools into a
single binary to save disk space.  MUT inspects `argv[0]` on start and invokes
the appropriate tool based on the name of the binary.
