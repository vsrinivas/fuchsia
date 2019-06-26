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
rustup toolchain link fuchsia $PWD/buildtools/linux-x64/rust && rustup default fuchsia
```

Run `fx gen-cargo //garnet/foo/path/to/target:label` for the GN target that you
want to work on.

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
