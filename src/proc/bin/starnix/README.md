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
$ fx test exit_test --output
```

You should see output like:

```
[==========] Running 3 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 3 tests from ExitTest
[ RUN      ] ExitTest.Success
[       OK ] ExitTest.Success (4 ms)
[ RUN      ] ExitTest.Failure
[       OK ] ExitTest.Failure (3 ms)
[ RUN      ] ExitTest.CloseFds
```

If you set the log level to `TRACE` (e.g.,  `fx log --severity TRACE --select "core/test*/*/starnix*#TRACE"`), you should e the system call handling in the device logs:

```
[629.603][starnix][D] 1[/data/tests/exit_test] wait4(0x3, 0x1c48095b950, 0x0, 0x0, 0x10, 0x10)
[629.603][starnix][D] 3[/data/tests/exit_test] prctl(0x53564d41, 0x0, 0x700d5ea000, 0x3000, 0x3a506c7a34b, 0xc06913ece9)
[629.603][starnix][D] 3[/data/tests/exit_test] -> 0x0
[629.604][starnix][D] 3[/data/tests/exit_test] exit_group(0x1, 0x3, 0x2b18e3464f8, 0x3000, 0x3a506c7a34b, 0xc06913ece9)
[629.604][starnix][I] exit_group: pid=3 exit_code=1
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
