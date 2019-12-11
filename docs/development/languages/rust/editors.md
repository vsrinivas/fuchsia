# Rust Editor Configuration

## Generating Cargo.toml files for use by editors {#generating-cargo-toml}

Many editors require Cargo.toml files in order to understand how your Rust
project is structured. These files can be generated using the following
commands, where `//garnet/foo/path/to/target:label` is the GN target that
you want to work on:

```sh
you@computer:/path/to/fuchsia $ fx build garnet/foo/path/to/target:some_label
you@computer:/path/to/fuchsia $ fx gen-cargo garnet/foo/path/to/target:some_label
```

Note that this label must point to a `rustc_...` GN template, not a Fuchsia package or other GN
target. For example:

```
rustc_binary("some_label") {
   ...
}
```
## Intellij

See instructions on [the Intellij Rust site](https://intellij-rust.github.io/).
Finally, follow the steps above to generate a Cargo.toml file for use by Intellij.

## Vim

See instructions on [`rust-lang/rust.vim`](https://github.com/rust-lang/rust.vim).

If you use Tagbar, see [this post](https://users.rust-lang.org/t/taglist-like-vim-plugin-for-rust/21924/13)
for instructions on making it work better with Rust.

## Visual Studio Code {#visual-studio-code}

The VS Code plugin uses the RLS (Rust language server) so you'll need to first
[install rustup](https://rustup.rs/). Next, install [this VS Code plugin].
You need to configure `rustup` to use the Fuchsia Rust toolchain.
Run this command from your Fuchsia source code root directory.

```sh
rustup toolchain link fuchsia $(scripts/youcompleteme/paths.py VSCODE_RUST_TOOLCHAIN)
rustup default fuchsia
```

Follow [the steps above](#generating-cargo-toml) to generate a `Cargo.toml` file
for use by VS Code.

Open VS Code and ensure that the directory where the generated `Cargo.toml` file
resides is added as a directory in your workspace (even though you probably have
its ancestor `fuchsia` directory already in your workspace). For example:

```sh
you@computer:/path/to/fuchsia $ fx build src/rusty/component:bin
you@computer:/path/to/fuchsia $ fx gen-cargo src/rusty/component:bin
```

In a new VS Code workspace, in this example, add both `/path/to/fuchsia` and
`/path/to/fuchsia/src/rusty/component` to the workspace. Saving the
workspace would yield something like:

`fuchsia_rusty_component.code-workspace`
```javascript
{
  "folders": [
    {
      "path": "/path/to/fuchsia"
    },
    {
      "path": "/path/to/fuchsia/src/rusty/component"
    }
  ]
}
```

Next, take note of the paths output by the following:

```sh
you@computer:/path/to/fuchsia $ ./scripts/youcompleteme/paths.py FUCHSIA_ROOT
you@computer:/path/to/fuchsia $ ./scripts/youcompleteme/paths.py VSCODE_RUST_TOOLCHAIN
```

Open VS Code settings

  * MacOS X: Code>Preferences>Settings
  * Linux: File>Preferences>Settings

Note there are different settings defined for each environment (for example, user vs remote development server).
In the upper right corner, click an icon whose mouse-over balloon tip says "Open Settings (JSON)".
Add the following settings:

```javascript
{
  // General rust and RLS configuration.
  "rust.target": "x86_64-fuchsia",
  "rust.target_dir": "<FUCHSIA_ROOT>/out/cargo_target",
  "rust.unstable_features": true,
  "rust-client.rlsPath": "<VS_CODE_TOOLCHAIN>/bin/rls",
  "rust-client.disableRustup": true,
  "rust.mode": "rls",

  // Read `Cargo.toml` from innermost root workspace directory.
  "rust-client.nestedMultiRootConfigInOutermost": false,

  // Optional extras:

  // Log RLS info/warning/error messages to a VSCode Output Panel.
  "rust-client.revealOutputChannelOn": "info",

  // Create `rls[numeric-id].log` in your project directory. Errors from RLS
  // will be logged there.
  "rust-client.logToFile": true,
}
```

[this VS Code plugin]: https://marketplace.visualstudio.com/items?itemName=rust-lang.rust

## emacs

### Synopsis

You will be using [flycheck](https://www.flycheck.org/en/latest/) to compile
your Rust files when you save them.  flycheck will parse those outputs and
highlight errors.  You'll also use
[flycheck-rust](https://github.com/flycheck/flycheck-rust) so that it will
compile with cargo and not with rustc.  Both are available from
[melpa](https://melpa.org/#/).

### Instructions

If you don't yet have melpa, follow the instructions
[here](https://melpa.org/#/getting-started).

Install `flycheck` and `flycheck-rust` in `M-x list-packages`.  Type `i`
to queue for installation what you are missing and then `x` to execute.

Next, make sure that flycheck-rust is run at startup.  Put this in your `.emacs` files:

```elisp
(with-eval-after-load 'rust-mode
  (add-hook 'flycheck-mode-hook #'flycheck-rust-setup))
```

You'll want cargo to run "check" and not "test" so set
`flycheck-rust-check-tests` to `nil`.  You can do this by typing `C-h v
flycheck-rust-check-tests<RET>` and then customizing the variable in the normal
way.

Now, you'll want to make sure that the default `cargo` and `rustc` that you are
using are Fuchsia versions of those.  From your fuchsia root, type:

```elisp
rustup toolchain link fuchsia $PWD/prebuilt/third_party/rust/linux-x64 && rustup default fuchsia
```

Finally, follow the steps at the top of this page to generate a Cargo.toml for the GN target
that you want to work on.

You can [read about](http://www.flycheck.org/en/latest/user/error-reports.html)
adjusting flycheck to display your errors as you like.  Type `C-h v
flycheck-highlighting-mode<RET>` and customize it.  Also customize `C-h v
flycheck-indiation-mode<RET>`.

Now restart emacs and try it out.

### Test and debug

To test that it works, you can run `M-x flycheck-compile` and see the
command-line that flycheck is using to check syntax.  It ought to look like one
of these depending on whether you are in a lib or bin:

```sh
cargo check --lib --message-format\=json
cargo check --bin recovery_netstack --message-format\=json
```

If it runs `rustc` instead of `cargo`, that's because you didn't `fx gen-cargo`.

Note that it might report errors on the first line of the current file.  Those are
actually errors from a different file.  The error's comment will name the
problematic file.
