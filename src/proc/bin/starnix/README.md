# Starnix

Starnix is a component that runs Linux binaries on Fuchsia. Currently, starnix is highly
experimental and can run only trivial programs.

## How to run starnix

Currently, we require a x86_64 host Linux system to run starnix.

### Configure your build

In order to run starnix, we need to build `//src/proc`:

```sh
$ fx set core.x64 --with //src/proc,//src/proc:tests
$ fx build
```

### Run Fuchsia

Run Fuchsia as normal, for example using `fx serve` and `fx emu -N`.

To monitor starnix, look for log messages with the `starnix` tag:

```sh
fx log --tag starnix --hide_metadata --pretty --severity TRACE --select "core/*/starnix*#TRACE"
```

When running tests, you will need to modify the selector for the logs.

```sh
fx log --tag starnix --hide_metadata --pretty --severity TRACE --select "core/test*/*/starnix*#TRACE"
```

The `select` arguments contain the moniker for the starnix instance you want to inspect logs from.

If you do not care about detailed logging, you can leave out the `--severity` and just do:

```
fx log --tag starnix --hide_metadata --pretty

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

### Run an interactive Android shell

To run an interactive Android shell, connected to your host machine, run:

```sh
$ ffx starnix shell
```
### Run a Linux test binary

Linux test binaries can also be run using the Starnix test runner using the
standard `fx test` command:

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

## Testing

### Running the in-process unit tests

Starnix also has in-process unit tests that can interact with its internals
during the test. To run those tests, use the following command:

```sh
$ fx test starnix-tests
```

### Using a locally built syscalls test binary

The `syscalls_test` test runs a prebuilt binary that has been built with the
Android NDK. You can substitute your own prebuilt binary using the
`starnix_syscalls_test_label` GN argument:

```sh
$ fx set core.x64 --args 'starnix_syscalls_test_label="//local/starnix/syscalls"' --with //src/proc,//src/proc:tests
```

Build your `syscalls` binary and put the file in `//local/starnix/syscalls`.
(If you are building using the Google-internal build system, be sure to
specific the `--config=android_x86_64` build flag to build an NDK binary.)

You can then build and run your test as usual:

```sh
$ fx build
$ fx test syscalls_test
```
