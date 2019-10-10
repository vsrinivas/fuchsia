# Development

## Building and testing core locally

Because `core` is agnostic to Fuchsia, you can build and run using `cargo`, which can save a bit of
development time. Follow the steps below to get a working local set up:

* Navigate to directory `//src/connectivity/network/netstack3/core`.
* If you haven't already, build netstack3 with `fx build netstack3-core`.
*  Run `fx gen-cargo .:netstack3-core`. That will create a new `Cargo.toml` file in that directory
(which you can also use to import into your IDE if you use one!).
> Note: the cargo generation is only a symbolic link to the fuchsia build output directory. You
should only need to run this once. You must have an `fx set` line that includes netstack3 for the
cargo file to be generated.
*  Add the following lines to your `~/.cargo/config` file (or create it if you don't have one),
*  making sure to replace the absolute path to your fuchsia directory and the target match to your
local development setup if different than `x86_64-unknown-linux-gnu`:
```toml
[target.x86_64-unknown-linux-gnu]
rustflags = ["-L", "absolute_path_to_fuchsia_directory/out/default/host_x64/obj/third_party/boringssl"]
```
* Run `cargo check` from the `core` directory to check that everything is correct.

### Troubleshooting

* If you can't run `cargo` correctly after running `jiri update`, clean up all `Cargo.lock` files in
`//src` and `//garnet` and try again.