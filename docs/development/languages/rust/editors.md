# Rust Editor Configuration

[TOC]

## Intellij

See instructions on [the Intellij Rust site](https://intellij-rust.github.io/).
Run `fx gen-cargo //garnet/foo/path/to/target:label` for the GN target that you want to work on and
open the corresponding directory.

## VIM

See instructions on [`rust-lang/rust.vim`](https://github.com/rust-lang/rust.vim).

## Visual Studio Code

The VS Code plugin uses the RLS (Rust language server) so you'll need to first
[install rustup](https://rustup.rs/). Next, install [this VSCode plugin].
You'll also have to tell `rustup` to use the Fuchsia Rust toolchain:
```sh
rustup toolchain link fuchsia /<your Fuchsia root>/buildtools/<platform>/rust
rustup default fuchsia
```

Next open File -> Preferences -> Settings (or type Ctrl+Comma). Add the following settings:
```javascript
{
  "rust.target": "x86_64-fuchsia",
  "rust.target_dir": "/<your Fuchsia root>/out/cargo_target",
  "rust.unstable_features": true,
  "rust-client.rlsPath": "/<your Fuchsia root>/buildtools/<platform>/rust/bin/rls",
  "rust-client.disableRustup": true,

  // Some optional settings that may help:
  "rust.goto_def_racer_fallback": true,
  "rust-client.logToFile": true,
}
```

Finally, run `fx gen-cargo //garnet/foo/path/to/target:label` for the GN target
that you want to work on and open the corresponding directory in VSCode.

[this VSCode plugin]: https://marketplace.visualstudio.com/items?itemName=rust-lang.rust
