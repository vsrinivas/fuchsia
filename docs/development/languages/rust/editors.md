# Rust editor configuration

As there is no specific editor for Rust development on Fuchsia, `vim` and `VS Code` are the
most popular options. However, documentation for setting up any editor is welcome in this document.

## `rust-analyzer` setup {#rust-analyzer}

[rust-analyzer](https://rust-analyzer.github.io/) is a [Language Server Protocol](https://microsoft.github.io/language-server-protocol/)
implementation for Rust. This is the recommended workflow and will work with minimal editor setup.

`rust-analyzer` uses a file in the `out/` directory called `rust-project.json` that is
generated based on the build graph at `gn gen` time. A symlink to the `rust-project.json` is located
in the root directory of the Fuchsia tree.

The `rust-project.json` file format is currently unstable. Sometimes this can cause an
unexpected version mismatch where GN produces a `rust-project.json` that `rust-analyzer` is
not expecting, causing `rust-analyzer` to not work correctly.

Currently, use [the latest version of `rust-analyzer`][rust-analyzer-latest].

## Alternative setup with `Cargo.toml` files
This is a volunteer-maintained workflow that exists because many tools in the Rust ecosystem assume
cargo integration. GN and Cargo have some design mismatches that may result in manual tweaks
being needed for the generated `Cargo.toml`s.

### Generating `Cargo.toml` files for use by editors {#generating-cargo-toml}

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

### Generating .cargo/config files for use by editor {#generating-cargo-config}

Some plugins require a `.cargo/config` file to allow cargo to operate correctly for Fuchsia
(e.g. to run `cargo check`). To easily generate this file, use the `fargo` tool.

1. [Install rustup](https://rustup.rs/)
2. Configure `rustup` to use the Fuchsia Rust toolchain by running:

    ```sh
    rustup toolchain link fuchsia $($FUCHSIA_DIR/scripts/youcompleteme/paths.py VSCODE_RUST_TOOLCHAIN)
    rustup default fuchsia
    ```

3. Clone and install the `fargo` tool within your `$FUCHSIA_DIR` by following the
[getting started instructions](https://fuchsia.googlesource.com/fargo/) for fargo.
4. Create your config:

    ```sh
    cd $FUCHSIA_DIR && fargo write-config
    # Note the caveats about changing architecture in the fargo readme
    # https://fuchsia.googlesource.com/fargo/#creating-a-cargo_config
    ```

## Intellij (Custom code completion)

See instructions on [the Intellij Rust site](https://intellij-rust.github.io/).
Finally, follow the steps above to generate a `Cargo.toml` file for use by Intellij.

## Vim

See instructions on [`rust-lang/rust.vim`](https://github.com/rust-lang/rust.vim).

If you use Tagbar, see [this post](https://users.rust-lang.org/t/taglist-like-vim-plugin-for-rust/21924/13)
for instructions on making it work better with Rust.

## Visual Studio Code {#visual-studio-code}

To use `rust-analyzer` with VSCode, use the latest stable version of
VSCode since `rust-analyzer` frequently depends on recent language server features.
VSCode can be downloaded from the
[official VSCode website][vscode-download].
It is recommended to:

* Keep automatic updates turned on (not available for Linux, see these
  [update instructions][vscode-update]).
* If you are working on confidential code,
  [disable telemetry reporting][vscode-disable-telemetry] as a precaution.

### `rust-analyzer` VSCode extension (supported workflow)

You can install the `rust-analyzer` extension directly
[from the VSCode marketplace][vscode-rust-analyzer].
If you notice that `rust-analyzer` is broken, it could be due to a breaking
change in the `rust-project.json` file. You may need to
[manually downgrade rust-analyzer][vscode-downgrade]
to [a currently supported version](#rust-analyzer).

Once you have installed the rust-analyzer extension, add the following
configurations to your `settings.json` file:

Note: To access the VS Code settings, click the **Code** menu, then **Preferences**, then **Settings**.
Scroll and click on **Edit in settings.json**.

```javascript
{
  // disable cargo check on save
  "rust-analyzer.checkOnSave.enable": false,
  "rust-analyzer.checkOnSave.allTargets": false,
}
```

### RLS (Alternative setup with Cargo)
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

Finally, follow the steps at the top of this page to generate a `Cargo.toml` for the GN target
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

## Sublime Text {#sublime-text}

### Using Rust-Enhanced for syntax checking

Follow the steps above to [generate a `Cargo.toml` file](#generating-cargo-toml) and also
the steps to [generate a `cargo/config` file](#generating-cargo-config), which will also
setup `cargo` to use the Fuchsia Rust toolchain.

Then, install the [Rust Enhanced](https://packagecontrol.io/packages/Rust%20Enhanced) plugin.
Now, you should have syntax checking on save and be able to run `cargo check` from the
context menu / command palette. Thanks to `fargo`, some tests also appear to run OK, but this
hasn't been thoroughly tested.

### Using a language server for intellisense / hover tooltips / go-to-definition

#### Setup

First, install the [LSP package](https://github.com/sublimelsp/LSP) for Sublime. Then, you
have two choices for the language server, pick one:

1. rust-analyzer (recommended): Follow the [rust-analyzer setup instructions]
(https://github.com/rust-analyzer/rust-analyzer/tree/master/docs/user) for Sublime.
2. RLS: Just enable `rls` in the `LSP: Enable Language Server` options from
the Sublime command palette.

#### Usage

In order for the language server to work, you need to open a folder that contains a `Cargo.toml`
as the root of your Sublime project. There are two ways you can do this:

1. Open a new Sublime window for the folder that contains the `Cargo.toml` (e.g.
`garnet/foo/path/to/target`)
2. Or, go to the top menu bar -> Project -> Add Folder to Project. This will keep all your files
inside one Sublime window, and works even if you have the broader `fuchsia` folder also open.

You may need to restart Sublime after these steps.

[rust-analyzer-latest]: https://github.com/rust-analyzer/rust-analyzer/releases
[vscode-download]: https://code.visualstudio.com/Download
[vscode-update]:  https://vscode-docs.readthedocs.io/en/stable/supporting/howtoupdate/
[vscode-disable-telemetry]: https://code.visualstudio.com/docs/getstarted/telemetry#_disable-telemetry-reporting
[vscode-rust-analyzer]: https://marketplace.visualstudio.com/items?itemName=matklad.rust-analyzer
[vscode-downgrade]: https://code.visualstudio.com/updates/v1_30#_install-previous-versions