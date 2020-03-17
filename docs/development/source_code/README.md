# Get Fuchsia source code

This guide provides instructions for the following tasks:

*   [Download the Fuchsia source code](#download-fuchsia-source).
*   [Set up environment variables](#set-up-environment-variables).

## Prerequisites

Fuchsia's [bootstrap script](/scripts/bootstrap) requires Python, cURL, unzip,
and Git to be up-to-date.

### Linux

Install or update the following packages:

```posix-terminal
sudo apt-get install build-essential curl git python unzip
```

### macOS

Do the following:

1.  Install the Xcode command line tool:

    ```posix-terminal
    xcode-select --install
    ```

1.  Install the latest version of
    [Xcode](https://developer.apple.com/xcode/){:.external}.

## Download Fuchsia source {#download-fuchsia-source}

Fuchsia's bootstrap script creates a `fuchsia` directory and downloads the
content of the Fuchsia source repository to this new directory.

To download the Fuchsia source, do the following:

1.  Go to the directory where you want to create your `fuchsia` directory, for
    example:

    ```posix-terminal
    cd ~
    ```

    Note: All examples and instructions in `fuchsia.dev` use `~/fuchsia` as the
    root directory of the Fuchsia project.

1.  Run the bootstrap script:

    ```posix-terminal
    curl -s "https://fuchsia.googlesource.com/fuchsia/+/master/scripts/bootstrap?format=TEXT" | base64 --decode | bash
    ```

    Downloading may take up to 60 minutes.

To learn how the Fuchsia source code is organized, see
[Source code layout](/docs/concepts/source_code/layout.md).

### Authentication error

If you see the `Invalid authentication credentials` error during the bootstrap
process, your `~/.gitcookies` file may contain cookies from some
repositories in `googlesource.com` that the bootstrap script
wants to check out anonymously.

To resolve this error, do one of the following:

*   Follow the onscreen directions to get passwords for the specified
    repositories.
*   Delete the offending cookies from the `.gitcookies` file.

## Set up environment variables {#set-up-environment-variables}

Setting up Fuchsia environment variables requires the following:

*   Add the `.jiri_root/bin` directory to your `PATH`.
*   Source the `scripts/fx-env.sh` file.

The `.jiri_root/bin` directory in the Fuchsia source contains
the [`jiri`](https://fuchsia.googlesource.com/jiri){:.external} and
[`fx`](/docs/development/build/fx.md) tools, which are essential to Fuchsia workflows.
Fuchsia uses the `jiri` tool to manage multiple repositories in the Fuchsia project.
The `fx` tool helps configure, build, run, and debug Fuchsia.
The Fuchsia toolchain requires `jiri` to be available in your `PATH`.

Additionally, sourcing the [`fx-env.sh`](/scripts/fx-env.sh) script
enables useful shell functions in your terminal. For
instance, it creates a `FUCHSIA_DIR` environment variable and
provides the `fd` command for navigating directories with auto-completion.
See comments in `fx-env.sh` for details.

### Update your shell script {#update-your-shell-script}

Update your shell script to automatically set up Fuchsia environment variables
in your terminal.

The following steps use a `bash` terminal as an example:

1.  Use a text editor to open your `~/.bashrc` file:

    ```posix-terminal
    vim ~/.bashrc
    ```

1.  Add the following lines your `~/.bashrc` file and save the file:

    Note: If your Fuchsia source code is not located in the `~/fuchsia` directory,
    replace `~/fuchsia` with your Fuchsia directory.

    ```sh
    export PATH=~/fuchsia/.jiri_root/bin:$PATH
    source ~/fuchsia/scripts/fx-env.sh
    ```

1.  Update your environment variables:

    ```posix-terminal
    source ~/.bashrc
    ```

    You can now run `jiri` and `fx` in any directory.

Run the following commands in any directory and confirm that these commands
print a usage guide for the tool:

```posix-terminal
jiri help
```

```posix-terminal
fx help
```

### Work on Fuchsia without updating your PATH

The following sections provide alternative approaches to the
[Update your shell script](#update-your-shell-script) section.

#### Copy the tool to your binary directory

If you don't wish to update your environment variables, but you want `jiri` to
work in any directory, copy the `jiri` tool to your `~/bin` directory, for
example:

Note: If your Fuchsia source code is not located in the `~/fuchsia` directory,
replace `~/fuchsia` with your Fuchsia directory.

```posix-terminal
cp ~/fuchsia/.jiri_root/bin/jiri ~/bin
```

However, you must have write access to the `~/bin` directory without `sudo`.
If you don't, `jiri` cannot keep itself up-to-date.

#### Add a symlink to your binary directory

Similarly, if you want to use the `fx` tool without updating your environment
variables, provide the `fx` tool's symlink in your `~/bin` directory, for
example:

Note: If your Fuchsia source code is not located in the `~/fuchsia` directory,
replace `~/fuchsia` with your Fuchsia directory.

```posix-terminal
ln -s ~/fuchsia/scripts/fx ~/bin
```

Alternatively, run the `fx` tool directly using its path, for example:

```posix-terminal
./scripts/fx help
```

In either case, you need `jiri` in your `PATH`.

## See also

For the next steps, see
[Configure and build Fuchsia](/docs/getting_started.md#configure-and-build-fuchsia)
in the Getting started guide.
