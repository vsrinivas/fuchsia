Fuchsia Source
==============

Fuchsia uses the `jiri` tool to manage git repositories
[https://fuchsia.googlesource.com/jiri](https://fuchsia.googlesource.com/jiri).
This tool manages a set of repositories specified by a manifest.

## Prerequisites

On Ubuntu:

 * `sudo apt-get install golang git-all build-essential curl unzip`

On Mac:

 * Install Xcode Command Line Tools
 * `brew install golang`

## Creating a new checkout

The bootstrap procedure requires that you have Go 1.6 or newer and Git
installed and on your PATH.  To create a new Fuchsia checkout in a directory
called `fuchsia` run the following commands. The `fuchsia` directory should
not exist before running these steps.

```
curl -s https://raw.githubusercontent.com/fuchsia-mirror/jiri/master/scripts/bootstrap_jiri | bash -s fuchsia
cd fuchsia
export PATH=`pwd`/.jiri_root/bin:$PATH
jiri import fuchsia https://fuchsia.googlesource.com/manifest
jiri update
```

### Working without altering your PATH

If you don't like having to mangle your environment variables, and you want
`jiri` to "just work" depending on your current working directory, there's a
shim for that.

```
sudo cp .jiri_root/bin/jiri /usr/local/bin
sudo chmod 755 /usr/local/bin/jiri
```

That script is just a wrapper around the real `jiri`.  It crawls your parent
directory structure until it finds a `.jiri_root` directory, then executes the
`jiri` it finds there.
