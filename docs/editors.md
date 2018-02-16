# Editor integration for Zircon

## YouCompleteMe

[YouCompleteMe](https://valloric.github.io/YouCompleteMe/) is a semantic
code-completion engine. YouCompleteMe works natively with Vim but it can also be
integrated with other editors through [ycmd](https://github.com/Valloric/ycmd).

### Install YouCompleteMe in your editor

See the [installation
guide](https://github.com/Valloric/YouCompleteMe#installation).

**Note**: Installing YCM on MacOS with Homebrew is not recommended because of
library compatibility errors. Use the official installation guide instead.

#### gLinux (Googlers only)

(This applies to anyone compiling on gLinux, even if editing over SSHFS on
MacOS) Ignore the above. Search the Google intranet for "YouCompleteMe" for
installation instructions.

### Generate compilation database

YouCompleteMe (and other tools like clang-tidy) require a [JSON compilation
database](https://clang.llvm.org/docs/JSONCompilationDatabase.html) that
specifies how each file is compiled. This database is normally stored in a file
called `compile_commands.json`. There are multiple ways to generate this
database for Zircon.

#### compiledb-generator

[compiledb-generator](https://github.com/nickdiego/compiledb-generator) is the
recommended way to generate a compilation database. It works by running make in
no-op mode and parsing the output. Thus it may be less accurate than bear, but
it should be able to produce a complete database very quickly and without
requiring a clean build.

To use it, download it from GitHub and run

```bash
cd "${ZIRCON_DIR}"
compiledb-gen-make <make args> > compile_commands.json
```

#### Bear

Bear intercepts `exec(3)` calls made during a build and scrapes commandlines
that look like C/C++ compiler invocations. Thus it is guaranteed to accurately
capture the commands used during compilation. However it can't incrementally
maintain a compilation database, so it requires performing a clean build.

##### Install Bear

You can try using your system's package manager (apt-get, brew) to install Bear,
but you will need version 2.2.1 or later to match the compiler names that Zircon
uses. Both homebrew's and Debian testing's versions are sufficiently new. You
can also fall back to installing it from
[GitHub](https://github.com/rizsotto/Bear).

##### Invoke Bear on the Zircon build system

You'll need to do this whenever the sources or makefiles change in a way that
affects includes or types, or when you add/delete/move files, though it doesn't
hurt to use a stale database.

As easy as:

```bash
cd "${ZIRCON_DIR}"
make clean
bear make -j$(getconf _NPROCESSORS_ONLN)
```

This will create a `compile_commands.json` file in the local directory.

### Use it

YouCompleteMe will use `compile_commands.json` to do code completion and find
symbol definitions/declarations. See your editor's YouCompleteMe docs for
details.

It should pick up the `json` file automatically. If you want to move it out of
the `zircon` tree, you can move the file to its parent directory.

## See also

For Fuchsia integration, see
https://fuchsia.googlesource.com/scripts/+/master/vim/README.md
