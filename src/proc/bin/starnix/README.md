# Starnix

Starnix is a component that runs Linux binaries on Fuchsia. Currently, starnix is highly
experimental and can run only trivial programs.

## How to run starnix

Currently, we require a x86_64 host Linux system to run starnix.

### Build example binary

We cannot currently build an appropriate binary with the Fuchsia build system, which means you'll
need to manually build a Linux binary.

```sh
$ cd src/proc/bin/starnix/fixtures
$ ./build.sh
```

Next, edit the `//src/proc/bin/starnix/BUILD.gn` file and uncomment the lines indicated by comments
about testing locally.

### Disable create_raw_processes security policy.

In `//src/sys/component_manager/src/elf_runner/mod.rs`, comment out the code block that prevents
direct creation of processes. To find the code block, look for the comment "Prevent direct
creation of processes".

### Add starnix to core.cml

In order to make starnix available in the system, we need to add starnix to the
core component by adding the following line to `//src/sys/core/meta/core.cml`,
directly above the `children` declaration:

```
    include: [ "src/proc/bin/starnix/meta/core.shard.cml" ],
```

### Configure your build

In order to run starnix, we need to build `//src/proc`:

```sh
$ fx set core.x64 --with-base //src/proc,//src/proc:tests
$ fx build
```

### Run Fuchsia

Run Fuchsia as normal, for example using `fx serve` and `fx emu -N`.

To monitor starnix, look for log messages with the `starnix` tag:

```sh
fx log --tag starnix
```

### Run a Linux binary

To run a Linux binary, ask starnix to start a component that wraps the binary:

```sh
$ ffx starnix start fuchsia-pkg://fuchsia.com/hello_starnix#meta/hello_starnix.cm
```

If this is the first time you've used the `ffx starnix` command, you might need
to configure `ffx` to enable the `starnix` commands. Attempting to run the
`start` command should provide instructions for enabling the `starnix` commands.

If everything is working, you should see some log messages like the following:

```
[00064.846853][33707][33709][starnix, starnix] INFO: main
[00064.847640][33707][33709][starnix, starnix] INFO: start_component: fuchsia-pkg://fuchsia.com/hello_starnix#meta/hello_starnix.cm
```
