Fuchsia Source
==============

Fuchsia uses the `jiri` tool to manage git repositories
[https://fuchsia.googlesource.com/jiri](https://fuchsia.googlesource.com/jiri).
This tool manages a set of repositories specified by a manifest.

For how to build, see Fuchsia's
[Getting Started](https://fuchsia.googlesource.com/docs/+/master/getting_started.md)
doc.

## Creating a new checkout

The bootstrap procedure requires that you have Go 1.6 or newer and Git
installed and on your PATH.

First, select the [layer](layers.md) of the system you wish to build. (If you're
unsure, select `topaz`, which contains the lower layers). Then, run the
following command:

```
curl -s "https://fuchsia.googlesource.com/scripts/+/master/bootstrap?format=TEXT" | base64 --decode | bash -s <layer>
```

This script will bootstrap a development environment for the given layer in a
directory named after the layer. Upon success, the script should print a message
recommending that you add the `.jiri_root/bin` directory to your PATH (which
you should do).

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

or just run the tool directly as `scripts/fx`.
