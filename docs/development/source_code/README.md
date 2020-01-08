# Fuchsia Source

Fuchsia uses the `jiri` tool to manage git repositories. This tool manages
a set of repositories specified by a manifest. Jiri is located at
[https://fuchsia.googlesource.com/jiri](https://fuchsia.googlesource.com/jiri).

To understand how the Fuchsia repository is organized,
see [Source code layout](/docs/development/source_code/layout.md).

To prepare your development environment and build Fuchsia,
see [Getting started](/docs/getting_started.md).

To submit your contribution to the Fuchsia project using `git` and
`jiri` commands,
see [Contribute changes](/docs/development/source_code/contribute_changes.md).

## Creating a new Fuchsia checkout

Fuchsia provides a bootstrap script that sets up your development environment
and syncs with the Fuchsia source repository. It requires that you have the
following installed and up to date:

 * Curl
 * Python
 * Unzip
 * Git

 1. To install these tools, run the following script. This command will install them if they are missing or update if they exist.

    ```
    sudo apt-get install build-essential curl git python unzip
    ```

 1. Go to the directory where you want to set up your workspace for the Fuchsia
    codebase. This can be anywhere, but this example uses your home directory.

    ```
    cd ~
    ```

 1. Run the script to bootstrap your development environment. This script
    automatically creates a `fuchsia` directory for the source code:

    ```
    curl -s "https://fuchsia.googlesource.com/fuchsia/+/master/scripts/bootstrap?format=TEXT" | base64 --decode | bash
    ```

Downloading Fuchsia source can take up to 60 minutes.

### Setting up environment variables

Upon success, the bootstrap script should print a message recommending that you
add the `.jiri_root/bin` directory to your PATH. This will add `jiri` to your
PATH, which is recommended and is assumed by other parts of the Fuchsia
toolchain.

Another tool in `.jiri_root/bin` is `fx`, which helps configuring, building,
running and debugging Fuchsia. See `fx help` for all available commands.

You can also source `scripts/fx-env.sh`, but sourcing `fx-env.sh` is not
required. It defines a few environment variables that are commonly used in the
documentation, such as `$FUCHSIA_DIR`, and provides useful shell functions, for
instance `fd` to change directories effectively. See comments in
`scripts/fx-env.sh` for more details.

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
OWNERS files. These list email addresses of individuals who are
familiar with and can provide code review for the contents of the
containing directory. See [owners.md](owners.md) for more
discussion.

## How to handle third-party code

See the [guidelines](third-party-metadata.md) on writing the metadata for
third-party code in README.fuchsia files.

## Troubleshooting

### Authentication errors

If you see an error when you check out the code warning you about `Invalid
authentication credentials`, you likely have a cookie in your
`$HOME/.gitcookies` file that applies to repositories that jiri tries to check
out anonymously (likely in the domain `.googlesource.com`).  You can follow the
onscreen directions to get passwords for the specific repositories, or you can
delete the offending cookie from your `.gitcookies` file.
