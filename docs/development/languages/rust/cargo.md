# Using cargo on Fuchsia

This is a volunteer-maintained workflow that exists because many tools in the Rust ecosystem assume
cargo integration. GN and Cargo have some design mismatches that may result in manual tweaks
being needed for the generated `Cargo.toml`s.

Because of this, cargo in the Fuchsia tree is **not** officially supported; things may break
from time to time.

### Generating `Cargo.toml` files {#cargo-toml-gen}

In order to generate the cargo files based on the build graph of GN, add `--cargo-toml-gen` to
the `fx set` and `fx args` commands. This adds a few seconds at `gn gen` time. For example:

```sh
fx set --cargo-toml-gen <normal fx args>
```

Most editors require the `Cargo.toml` file to be in a location that is adjacent to the `src/` directory.
Symlinks to these files can be generated using the following commands, where
`//garnet/foo/path/to/target:label` is the GN target that you want to work on:

```sh
fx gen-cargo garnet/foo/path/to/target:some_label
```

Note that this label must point to a [`rustc_...` GN template](README.md#build)
(not a Fuchsia package or other GN target). For example:

```
rustc_binary("some_label") {
   ...
}
```

### Generating .cargo/config files {#cargo-config-gen}

Some plugins require a `.cargo/config` file to allow cargo to operate correctly for Fuchsia
(e.g. to run `cargo check`). To easily generate this file, use the [`fargo`][fargo] tool.

1. [Install rustup](https://rustup.rs/)
2. Configure `rustup` to use the Fuchsia Rust toolchain by running:

    ```sh
    rustup toolchain link fuchsia $($FUCHSIA_DIR/scripts/youcompleteme/paths.py VSCODE_RUST_TOOLCHAIN)
    rustup default fuchsia
    ```

3. Clone and install the `fargo` tool within your `$FUCHSIA_DIR` by following the
[getting started instructions][fargo] for fargo.
4. Create your config:

    ```sh
    cd $FUCHSIA_DIR && fargo write-config
    # Note the caveats about changing architecture in the fargo readme
    # https://fuchsia.googlesource.com/fargo/#creating-a-cargo_config
    ```

[fargo]: https://fuchsia.googlesource.com/fargo/
