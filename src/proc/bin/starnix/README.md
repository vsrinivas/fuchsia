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

### Configure your build

In order to run starnix, we need to build both `//src/proc` and `//examples`:

```sh
$ fx set core.x64 --with //examples,//src/proc,//src/proc:tests
$ fx build
```

Run Fuchsia as normal, for example using `fx serve` and `fx emu -N`.

To monitor starnix, look for log messages with the `starnix` tag:

```sh
fx log --tag starnix
```

Now, you're ready to run starnix. Unfortunately, component framework v2 does not have a way to
start components manually (see https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=70952), so we
need to start a nested v2 component manager inside the component framework v1 and instruct that
component manager to run starnix:

```sh
$ fx shell 'run fuchsia-pkg://fuchsia.com/components-basic-example#meta/component_manager_for_examples.cmx fuchsia-pkg://fuchsia.com/starnix#meta/starnix_manager.cm'
```

If everything is working, you should see some log messages like the following:

```
[00064.846853][33707][33709][starnix, starnix] INFO: main
[00064.847640][33707][33709][starnix, starnix] INFO: start_component: fuchsia-pkg://fuchsia.com/hello_starnix#meta/hello_starnix.cm
```
