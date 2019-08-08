# Editor integration for Zircon

## YouCompleteMe

[YouCompleteMe](http://ycm-core.github.io/YouCompleteMe/) is a semantic
code-completion engine. YouCompleteMe works natively with Vim but it can also be
integrated with other editors through [ycmd](https://github.com/Valloric/ycmd).

### Install YouCompleteMe in your editor

See the [installation
guide](https://github.com/Valloric/YouCompleteMe#installation).

Note: Installing YCM on MacOS with Homebrew is not recommended because of
library compatibility errors. Use the official installation guide instead.

#### gLinux (Googlers only)

(This applies to anyone compiling on gLinux, even if editing over SSHFS on
MacOS) Ignore the above. Search the Google intranet for "YouCompleteMe" for
installation instructions.

### Generate compilation database

YouCompleteMe (and other tools like clang-tidy) require a [JSON compilation
database](https://clang.llvm.org/docs/JSONCompilationDatabase.html) that
specifies how each file is compiled. This database is normally stored in a file
called `compile_commands.json`. 

The following will create a `compile_commands.json` file in the local directory:

```gn
gn gen build-zircon --export-compile-commands
```

### Use it

YouCompleteMe will use `compile_commands.json` to do code completion and find
symbol definitions/declarations. See your editor's YouCompleteMe docs for
details.

It should pick up the `json` file automatically. If you want to move it out of
the `zircon` tree, you can move the file to its parent directory.

## See also

For Fuchsia integration, see
https://fuchsia.googlesource.com/fuchsia/+/master/scripts/vim/README.md
