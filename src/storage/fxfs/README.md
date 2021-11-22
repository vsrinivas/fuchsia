Fxfs is a filesystem under active development. See the [RFC] for the motivation
and higher level design decisions.

Fxfs has rustdoc style documentation which can be generated and opened with
commands like:

```shell
# Configure the build to generate cargo output.
$ fx set core.x64 --goma --auto-dir --with //src/storage/fxfs:tests --cargo-toml-gen
# Rebuild to actually generates cargo .toml files.
$ fx build
# Generate rustdoc.
$ fx rustdoc --doc-private //src/storage/fxfs:lib --open
```

Note: The last line prints the documentation location, which depends on target.

[RFC]: /docs/contribute/governance/rfcs/0136_fxfs.md
