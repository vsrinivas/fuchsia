# Get Fuchsia source code

This guide provides instructions for the following tasks:

*   [Download the Fuchsia source code](#download-fuchsia-source).
*   [Set up environment variables](#set-up-environment-variables).

## Prerequisites

The Fuchsia project requires `python`, `curl`, `unzip`, and `git` to be
up-to-date:

*   For Linux, install or update the following packages:

    ```posix-terminal
    sudo apt-get install build-essential curl git python unzip
    ```

    Note: Fuchsia recommends the version of Git to be 2.28 or higher.

*   For macOS, install the Xcode command line tools:

    ```posix-terminal
    xcode-select --install
    ```

## Download Fuchsia source {#download-fuchsia-source}

Fuchsia's [bootstrap script](/scripts/bootstrap) creates a `fuchsia` directory
and downloads the content of the Fuchsia source repository to this new
directory.

Note: Downloading Fuchsia may take up to 60 minutes.

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

    If you see the `Invalid authentication credentials` error during the
    bootstrapping process, see [Authentication error](#authentication-error) for
    help.

## Set up environment variables {#set-up-environment-variables}

Setting up Fuchsia environment variables consists of the following:

*   Add the `.jiri_root/bin` directory to your `PATH`.

    The `.jiri_root/bin` directory in the Fuchsia source contains the
    <code>[jiri](https://fuchsia.googlesource.com/jiri){:.external}</code> and
    <code>[fx](/docs/development/build/fx.md)</code> tools are essential to
    Fuchsia workflows. Fuchsia uses the `jiri` tool to manage repositories in
    the Fuchsia project. The `fx` tool helps configure, build, run, and debug
    Fuchsia. The Fuchsia toolchain requires `jiri` to be available in your
    `PATH`.

*   Source the `scripts/fx-env.sh` file.

    Although it's not required, sourcing the
    <code>[fx-env.sh](/scripts/fx-env.sh)</code> script enables useful shell
    functions in your terminal. For instance, it creates a `FUCHSIA_DIR`
    environment variable and provides the `fd` command for navigating
    directories with auto-completion (see comments in `fx-env.sh` for more
    information).

### Update your shell script {#update-your-shell-script}

Update your shell script to automatically set up Fuchsia environment variables
in your terminal.

Note: If you don't wish to update your environment variables, see
[Work on Fuchsia without updating your PATH](#work-on-fuchsia-without-updating-your-path).

The following steps use a `bash` terminal as an example. If you are using `zsh` replace
`~/.bashrc` with `~/.zshrc` in the following example.

1.  Use a text editor to open your `~/.bashrc` file:

    ```posix-terminal
    vim ~/.bashrc
    ```

1.  Add the following lines your `~/.bashrc` file and save the file:

    Note: If your Fuchsia source code is not located in the `~/fuchsia`
    directory, replace `~/fuchsia` with your Fuchsia directory.

    ```sh
    export PATH=~/fuchsia/.jiri_root/bin:$PATH
    source ~/fuchsia/scripts/fx-env.sh
    ```

1.  Update your environment variables:

    ```posix-terminal
    source ~/.bashrc
    ```

1.  Verify that you can run the following commands from any directory without
    error:

    ```posix-terminal
    jiri help
    ```

    ```posix-terminal
    fx help
    ```

## Next steps

See
[Configure and build Fuchsia](/docs/get-started/build_fuchsia.md)
in the Getting started guide for the next steps.


## Troubleshoot

### Authentication error {#authentication-error}

If you see the `Invalid authentication credentials` error during the bootstrap
process, your `~/.gitcookies` file may contain cookies from some repositories in
`googlesource.com` that the bootstrap script wants to check out anonymously.

To resolve this error, do one of the following:

*   Follow the onscreen directions to get passwords for the specified
    repositories.
*   Delete the offending cookies from the `.gitcookies` file.

### Work on Fuchsia without updating your PATH {#work-on-fuchsia-without-updating-your-path}

The following sections provide alternative approaches to the
[Update your shell script](#update-your-shell-script) section:

*   [Copy the tool to your binary directory](#copy-the-tool-to-your-binary-directory)
*   [Add a symlink to your binary directory](#add-a-symlink-to-your-binary-directory)

#### Copy the tool to your binary directory {#copy-the-tool-to-your-binary-directory}

If you don't wish to update your environment variables, but you want `jiri` to
work in any directory, copy the `jiri` tool to your `~/bin` directory, for
example:

Note: If your Fuchsia source code is not located in the `~/fuchsia` directory,
replace `~/fuchsia` with your Fuchsia directory.

```posix-terminal
cp ~/fuchsia/.jiri_root/bin/jiri ~/bin
```

However, you must have write access to the `~/bin` directory without `sudo`. If
you don't, `jiri` cannot keep itself up-to-date.

#### Add a symlink to your binary directory {#add-a-symlink-to-your-binary-directory}

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

