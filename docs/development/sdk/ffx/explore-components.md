# ffx component explore

`ffx component explore` is an experimental ffx plugin that allows you to
interactively explore the internals of fuchsia components. Unlike `fx shell`,
you only get access to your component's incoming & outgoing capabilities. This
means you're able to explore in an environment almost identical to what your
component sees.

Currently, it launches a [Dash](#what_is_dash) process scoped to your component.
In this process:

+   You have a similar command-line experience to `fx shell`.
+   You can use familiar tools such as `ls`, `cat` and `grep`.
+   You can explore your component's incoming and outgoing capabilities.

Note: The filesystem used in `Dash` is not POSIX-compliant. Capabilities are
presented as files and directories to aid in exploration.

## Getting started

1.  Ensure your branch is synced to tip-of-tree with `jiri update`.

1.  Ensure that you are building an engineering product like `core`, `terminal`
    or `workstation_eng`.

1.  Start up an emulator and/or connect to a device.

1.  Choose a component moniker and start exploring:

    ```none
    > ffx component explore /bootstrap/archivist
    $ ls
    bin
    exposed
    ns
    out
    runtime
    svc
    ```

    See [what is the namespace root](#what_is_root) for more details on these
    directories.

## How do I...

For the following sections, we use the `/bootstrap/archivist` moniker. This
should be replaced with the moniker of your component.

### Explore the capabilities available to my component?

The `/ns` directory contains the component's namespace, exactly as your
component would see it.

```none
> ffx component explore /bootstrap/archivist
$ cd ns
$ ls
pkg
data
svc
$ cd data
$ ls
logs.txt
$ mv logs.txt logs2.txt
$ mkdir captures
```

### Explore the capabilities exposed by my component?

The `/exposed` directory contains the capabilities exposed from your component
to its parent.

```none
> ffx component explore /bootstrap/archivist
$ cd exposed
$ ls
fuchsia.foo.bar.MyProtocol
```

### Explore the capabilities my component is serving?

If a component is running, the `/out` directory contains all the capabilities
currently served by it.

```none
> ffx component explore /bootstrap/archivist
$ cd out
$ ls
diagnostics
svc
$ cd svc
$ ls
fuchsia.foo.bar.MyProtocol
```

### Explore debug runtime data about my component?

If your component is running, the `/runtime` directory contains debug
information provided by the component runner.

```none
> ffx component explore /bootstrap/archivist
$ cd runtime/elf
$ ls
args
process_id
job_id
$ cat process_id
7352
```

### Run a command-line tool I built for my component?

This isn't supported yet, but we're working on it. Soon, we will add support for
running custom command-line tools that can use your component's incoming/exposed
capabilities.

This is great because:

+   Current models for running tools on device are deprecated or provide too
    much privilege.
    +   The ongoing deprecation of `.cmx` components means that we will no
        longer have support for CMX shell tools.
    +   Raw binaries run in `fx shell` have access to more capabilities than
        they need.
+   Tools do not need to know the component topology to access capabilities.
    +   `ffx component explore` provides a predictable directory structure that
        isn't tied to the component's moniker.
+   Tools often need capabilities exposed by the component they are built for.
    +   `net` uses `fuchsia.net.*` capabilities exposed by the netstack
        component.
    +   `activity-ctl` uses `fuchsia.activity.*` capabilities exposed by the
        `activity` component.
    +   `stash-ctl` uses fuchsia.stash.* capabilities exposed by the stash
        component.

## FAQ

### What is Dash?

[Dash]{:.external} is the command interpreter used in `fx shell`, serial
console, terminal windows, virtcon, etc. We are using it as the experience for
`ffx component explore` because it is familiar and serves as a good starting
point for `cd`-ing and `ls`-ing around to explore your component.

### Why can't I see child components from the parent?

We do not allow accessing child components directly from the parent. Using
knowledge of the component topology to access a child component's capabilities
made tools brittle in `fx shell`. Tools used to keep hard-coded paths to
`/hub-v2` which encoded knowledge about the system topology.

As an alternative, we recommend:

+   explicitly routing capabilities from the child to the parent component.
+   exploring the child component itself.

### How is this different from ffx component run?

`ffx component run` creates and then starts a component in a specified
collection within the component topology. It offers no interactive capabilities.
`ffx component explore` allows exploring any existing component in the topology
interactively. You can use `ffx component explore` to learn about a component
you just created using `ffx component run`.

### What is the namespace root (/) in ffx component explore? {: #what_is_root}

`ffx component explore` creates a virtual file system at the namespace root that
contains the following directories:

| Directory  | Description                                                   |
| ---------- | ------------------------------------------------------------- |
| `/bin`     | contains a standard set of tools such as `ls`, `cat`, `mkdir` |
:            : etc.                                                          :
| `/exposed` | contains all exposed capabilities.                            |
| `/ns`      | contains the component's namespace, exactly as your component |
:            : would see it.                                                 :
| `/svc`     | contains capabilities needed by `Dash`.                       |

If your component is running, the following directories are also present:

Directory  | Description
---------- | ------------------------------------------------------------------
`/out`     | contains all capabilities currently being served by the component.
`/runtime` | contains debug information served by the component’s runner.

### Can I access Zircon handles or make FIDL calls via the Dash shell?

That is not supported directly from the command interpreter.

### How do I file a feature request for $NEW_FEATURE?

File all feature requests under the
[`ComponentFramework > Tools`][cf-tools-monorail] monorail component

[Dash]: https://manpages.debian.org/testing/dash/dash.1.html
[cf-tools-monorail]: https://bugs.fuchsia.dev/p/fuchsia/issues/entry?template=ComponentFramework&components=ComponentFramework%3ETools
