Fuchsia Source
==============

Fuchsia uses the `jiri` tool to manage git repositories
[https://fuchsia.googlesource.com/jiri](https://fuchsia.googlesource.com/jiri).
This tool manages a set of repositories specified by a manifest.

For how to build, see Fuchsia's [Getting Started](/getting_started.md) doc.

## Creating a new checkout

The bootstrap procedure requires that you have Go 1.6 or newer and Git
installed and on your PATH.

First, select the [layer](layers.md) of the system you wish to build. If you're
unsure, select `topaz`, which contains the lower layers. If you change your mind
later, you can use `fx set-layer <layer>` to [pick another
layer](/development/workflows/multilayer_changes.md#switching-between-layers).

Then, run the following command:

```
curl -s "https://fuchsia.googlesource.com/scripts/+/master/bootstrap?format=TEXT" | base64 --decode | bash -s <layer>
```

This script will bootstrap a development environment for the given layer
by first creating directories `fuchsia`, then downloading repositories for
`<layer>` as well as dependent repositories required in building `<layer>`.


### Setting up environment variables

Upon success, the bootstrap script should print a message recommending that you
add the `.jiri_root/bin` directory to your PATH. This will add `jiri` to your
PATH, which is strongly recommended and is assumed by other parts of the Fuchsia
toolchain.

Another tool in `.jiri_root/bin` is `fx`, which helps configuring, building,
running and debugging Fuchsia. See `fx help` for all available commands.

We also suggest sourcing `scripts/fx-env.sh`. It defines a few environment
variables that are commonly used in the documentation, such as `$FUCHSIA_DIR`,
and provides useful shell functions, for instance `fd` to change directories
effectively. See comments in `scripts/fx-env.sh` for more details.

### Working without altering your PATH

If you don't like having to mangle your environment variables, and you want
`jiri` to "just work" depending on your current working directory, just copy
`jiri` into your PATH.  However, **you must have write access** (without `sudo`)
to the **directory** into which you copy `jiri`.  If you don't, then `jiri`
will not be able to keep itself up-to-date.

```
cp .jiri_root/bin/jiri ~/bin
```

To use the `fx` tool, you can either symlink it into your `~/bin` directory:

```
ln -s `pwd`/scripts/fx ~/bin
```

or just run the tool directly as `scripts/fx`. Make sure you have **jiri** in
your PATH.

## Who works on the code

In the root of every repository and in many other directories are
MAINTAINERS files. These list email addresses of individuals who are
familiar with and can provide code review for the contents of the
containing directory. See [maintainers.md](maintainers.md) for more
discussion.

## How to handle third-party code

See the [guidelines](README.fuchsia.md) on writing README.fuchsia files.
