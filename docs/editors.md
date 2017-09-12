# Editor integration for Zircon

## YouCompleteMe for Vim/Atom

[YouCompleteMe](https://valloric.github.io/YouCompleteMe/) is a semantic
code-completion engine for Vim and Atom. You can use
[Bear](https://github.com/rizsotto/Bear) to build its database from the Zircon
makefiles.

### Install YouCompleteMe in your editor

Vim: https://valloric.github.io/YouCompleteMe/#installation

Atom: https://atom.io/packages/you-complete-me

### Install Bear 2.2.1 or later

Bear intercepts `exec(3)` calls made during a build and scrapes commandlines
that look like C/C++ compiler invocations.

You can try using your system's package manager (apt-get, brew) to install Bear,
but you will need version 2.2.1 or later to match the compiler names that
Zircon uses.

Example installation:

``` bash
mkdir "${HOME}/src"
cd "${HOME}/src"
git clone https://github.com/rizsotto/Bear.git
cd Bear
mkdir OUT
cd OUT

# If you want to install a copy for only your user:
  mkdir "${HOME}/local"
  cmake -DCMAKE_INSTALL_PREFIX="${HOME}/local" ..
  # And add ${HOME}/local/bin to your PATH.
# Or, to default to /usr/local:
  cmake ..

make all
make install  # Or 'sudo make install' to install to /usr/local
```

### Invoke Bear on the Zircon build system

You'll need to do this whenever the sources or makefiles change in a way that
affects includes or types, or when you add/delete/move files, though it doesn't
hurt to use a stale database.

As easy as:

``` bash
cd "${ZIRCON_DIR}"
make clean
bear make -j20
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
