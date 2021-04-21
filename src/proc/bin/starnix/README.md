# Starnix

Starnix is a component that runs Linux binaries on Fuchsia. Currently, starnix is highly
experimental and can run only trivial programs.

## How to run starnix

Currently, we require a x86_64 host Linux system to run starnix.

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

> Note: If you use `--with` instead of `--with-base`, the Fuchsia system might
not boot because adding the `core.shard.cml` to the core component starts
Starnix during system boot.

### Run Fuchsia

Run Fuchsia as normal, for example using `fx serve` and `fx emu -N`.

To monitor starnix, look for log messages with the `starnix` tag:

```sh
fx log --tag starnix
```

### Run a Linux binary

To run a Linux binary, ask starnix to start a component that wraps the binary:

```sh
$ ffx starnix start fuchsia-pkg://fuchsia.com/hello-starnix#meta/hello_starnix.cm
```

If this is the first time you've used the `ffx starnix` command, you might need
to configure `ffx` to enable the `starnix` commands. Attempting to run the
`start` command should provide instructions for enabling the `starnix` commands.

If everything is working, you should see some log messages like the following:

```
[00064.846853][33707][33709][starnix, starnix] INFO: main
[00064.847640][33707][33709][starnix, starnix] INFO: start_component: fuchsia-pkg://fuchsia.com/hello-starnix#meta/hello_starnix.cm
```

### Run a Linux test binary

Linux test binaries can be run using the starnix test runner. This can be done using the standard `fx test` command:

```
$ fx test hello-starnix-test --output
```

You should see output like:

```
Running test 'fuchsia-pkg://fuchsia.com/hello-starnix-test#meta/hello_starnix_test.cm'
[RUNNING]       fuchsia-pkg://fuchsia.com/hello-starnix-test#meta/hello_starnix_test.cm
[PASSED]        fuchsia-pkg://fuchsia.com/hello-starnix-test#meta/hello_starnix_test.cm
```

In the device logs you should see output like:

```
[starnix, starnix_runner] INFO: main
[starnix, starnix_runner] INFO: start_component: fuchsia-pkg://fuchsia.com/hello-starnix-test#meta/hello_starnix_test.cm
[starnix, strace] INFO: 1(0x1, 0x35a3ca6000, 0xe, 0x0, 0x0, 0x0)
[starnix, starnix_runner::syscalls] INFO: write: hello starnix

[starnix, strace] INFO: -> 0xe
[starnix, strace] INFO: 60(0x0, 0x35a3ca6000, 0xe, 0x0, 0x0, 0x0)
[starnix, starnix_runner::syscalls] INFO: exit: error_code=0
[starnix, strace] INFO: -> 0x0
```
