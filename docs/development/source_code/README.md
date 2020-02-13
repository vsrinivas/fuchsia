# Get Fuchsia source code

This page provides instructions on how to download Fuchsia source code and
set up environment variables for working on Fuchsia.

## Prerequisites

Fuchsia provides a bootstrap script that sets up your development environment
and syncs with the Fuchsia source repository. The script requires the
following tools to be up-to-date:

 * Curl
 * Python
 * Unzip
 * Git

To install or update these tools, run the following script:

```
sudo apt-get install build-essential curl git python unzip
```

## Download Fuchsia source

Once you install the prerequisite tools, do the following:

 1. Go to the directory where you want to set up your workspace for the Fuchsia
    codebase. This can be anywhere, but this example uses your home directory.

    ```
    cd ~
    ```

 1. Run the script to bootstrap your development environment. This script
    automatically creates a `fuchsia` directory for the source code.

    ```
    curl -s "https://fuchsia.googlesource.com/fuchsia/+/master/scripts/bootstrap?format=TEXT" | base64 --decode | bash
    ```

Downloading Fuchsia source can take up to 60 minutes. To understand how the Fuchsia repository is organized,
see [Source code layout](/docs/concepts/source_code/layout.md).

### Authentication errors

When checking out the code, if you see the `Invalid
authentication credentials` error, it means that your
`$HOME/.gitcookies` file already contains a cookie
(likely in the `.googlesource.com` domain) that applies to
the repositories that the script tries to check out anonymously.

In this case, do one of the following:

*  Follow the onscreen directions to get passwords for the specific
   repositories.
*  Delete the offending cookie from the `.gitcookies` file.

## Set up environment variables

Fuchsia uses the `jiri` tool to manage git repositories. This tool manages
a set of repositories specified by a manifest. (The `jiri` tool is located at
[https://fuchsia.googlesource.com/jiri](https://fuchsia.googlesource.com/jiri){:.external}.)

Upon successfully downloading Fuchsia source, the bootstrap script prints
a message recommending that you add the `.jiri_root/bin` directory to
your PATH.

Note: Adding `jiri` to your PATH is assumed by
other parts of the Fuchsia toolchain.

To show how to set up environement variables, the following steps uses
a `bash` terminal as example:

1. Add the `export` and `source` commands to your `.bashrc` script:

   ```
   $ cat >> ~/.bashrc <<EOL
   
   # Fuchsia
   # If you use a custom directory, adjust accordingly
   export PATH=~/fuchsia/.jiri_root/bin:$PATH
   source ~/fuchsia/scripts/fx-env.sh
   EOL
   ```
1. To update your environment with the new changes, run the following command:

   ```
   source ~/.bashrc
   ```


Another tool in `.jiri_root/bin` is `fx`, which helps configuring, building,
running and debugging Fuchsia. See [fx workflow](/docs/development/build/fx.md) for details.

You can also source `scripts/fx-env.sh`, but sourcing `fx-env.sh` is not
required. This script defines a few environment variables that are commonly used in the
documentation (such as `$FUCHSIA_DIR`) and provides useful shell functions (for
instance, `fd` to change directories effectively). See comments in
`scripts/fx-env.sh` for more details.

### Work on Fuchsia without altering your PATH

If you don't like having to mangle your environment variables, and you want
`jiri` to "just work" depending on your current working directory, copy
`jiri` into your PATH.  However, you must have write access (without `sudo`)
to the directory into which you copy `jiri`. If you don't, then `jiri`
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

## See also

For the next steps, see [Build Fuchsia](/docs/getting_started.md#build_fuchsia) in
the Getting started guide.

