# Starnix

Starnix runs unmodified Linux binaries on Fuchsia. We have tested Starnix with
glibc and bionic based binaries. Patches that improve Linux compatibility are
welcome!

## How to run starnix

### Configure your build

In order to run starnix, we need to build `//src/proc`:

```sh
fx set core.x64 --with //src/proc,//src/proc:tests
fx build
```

### Run Fuchsia

Run Fuchsia as normal, for example using `fx serve` and `ffx emu start --net user --headless`.

To monitor starnix, look for log messages with the `starnix` tag:

```sh
fx log --tag starnix --hide_metadata --pretty --severity TRACE --select "core/*/starnix*#TRACE"
```

When running tests, you will need to modify the selector for the logs.

```sh
fx log --tag starnix --hide_metadata --pretty --severity TRACE --select "core/test*/*/starnix*#TRACE"
```

The `--select` arguments contain the moniker for the starnix instance whose minimum severity log
level you want to change. This affects the logs emitted by starnix, as opposed to `--severity`,
which affects which logs are filtered for viewing. The changed log level only persists for the
duration of the `fx log` command.

If you do not care about detailed logging, you can leave out the `--severity` and just do:

```sh
fx log --tag starnix --hide_metadata --pretty

```

Starnix produces a large amount of logs and this can overload archivist's ability to
retain them, instead printing messages like:

```text
[starnix] WARNING: Dropped logs count: 5165
```

If you see this, you can reduce or eliminate the dropped messages by increasing
the value of `max_cached_original_bytes` the the JSON file
`//src/diagnostics/archivist/configs/archivist_config.json' in your local checkout.
Increasing by a facfor of 10 seems to work well.

### Run a Linux binary

To run a Linux binary, ask starnix to start a component that wraps the binary:

```sh
ffx starnix start fuchsia-pkg://fuchsia.com/hello-starnix#meta/hello_starnix.cm
```

If this is the first time you've used the `ffx starnix` command, you might need
to configure `ffx` to enable the `starnix` commands. Attempting to run the
`start` command should provide instructions for enabling the `starnix` commands.

If everything is working, you should see some log messages like the following:

```text
[00064.846853][33707][33709][starnix, starnix] INFO: main
[00064.847640][33707][33709][starnix, starnix] INFO: start_component: fuchsia-pkg://fuchsia.com/hello-starnix#meta/hello_starnix.cm
```

### Run an interactive Android shell

To run an interactive Android shell, connected to your host machine, run:

```sh
ffx starnix shell
```

### Running the ADB Relay

The target daemon 'adbd` is normally started as part of init.
If adbd is not already started, it can be started with

```sh
ffx starnix start fuchsia-pkg://fuchsia.com/starnix_android#meta/adbd.cm
```

The relay between the host and server is started by running the adb
starnix plugin in a termainal.

```sh
ffx starnix adb
```

After the plugin is running, connect to it with `adb connect` on
the host.

```sh
$ adb connect 127.0.0.1:5556
$ adb devices -l
List of devices attached
127.0.0.1:5556         device product:starnix_x86_64 model:starnix_x86_64 device:starnix_x86_64 transport_id:2
emulator-5554          offline transport_id:1
```

If there is more than one adb device, run commands with the '-s`
or `-t` selectors. For example:

```sh
adb -t 2 shell
adb -s 127.0.0.1:5556 logcat
```

### Run a Linux test binary

Linux test binaries can also be run using the Starnix test runner using the
standard `fx test` command:

```sh
fx test exit_test --output
```

You should see output like:

```text
[==========] Running 3 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 3 tests from ExitTest
[ RUN      ] ExitTest.Success
[       OK ] ExitTest.Success (4 ms)
[ RUN      ] ExitTest.Failure
[       OK ] ExitTest.Failure (3 ms)
[ RUN      ] ExitTest.CloseFds
```

If you set the log level to `TRACE` (e.g.,  `fx log --severity TRACE --select "core/test*/*/starnix*#TRACE"`), you should see the system call handling in the device logs:

```text
[629.603][starnix][D] 1[/data/tests/exit_test] wait4(0x3, 0x1c48095b950, 0x0, 0x0, 0x10, 0x10)
[629.603][starnix][D] 3[/data/tests/exit_test] prctl(0x53564d41, 0x0, 0x700d5ea000, 0x3000, 0x3a506c7a34b, 0xc06913ece9)
[629.603][starnix][D] 3[/data/tests/exit_test] -> 0x0
[629.604][starnix][D] 3[/data/tests/exit_test] exit_group(0x1, 0x3, 0x2b18e3464f8, 0x3000, 0x3a506c7a34b, 0xc06913ece9)
[629.604][starnix][I] exit_group: pid=3 exit_code=1
```

For GUnit tests (such as the syscall tests in //src/proc/tests/android/gvisor),
you can override the `--gunit_filter` argument, which excludes or include tests,
by specifying an alternate flag after `--` on the command line. For example,

```sh
fx test epoll_test --output -- --gunit_filter="*"
```

Specifying `*` as the filter turns on all tests in the binary.


## Testing

### Writing in-process unit tests

Decorate your test function with the `#[::fuchsia::test]` macro instead of the standard `#[test]`
macro. `#[::fuchsia::test]` will initialize logging so that failing tests can be debugged more
easily.

### Running the in-process unit tests

Starnix also has in-process unit tests that can interact with its internals
during the test. To run those tests, use the following command:

```sh
fx test starnix-tests
```

### Using a locally built syscalls test binary

The `syscalls_test` test runs a prebuilt binary that has been built with the
Android NDK. You can substitute your own prebuilt binary using the
`starnix_syscalls_test_label` GN argument:

```sh
fx set core.x64 --args 'starnix_syscalls_test_label="//local/starnix/syscalls"' --with //src/proc,//src/proc:tests
```

Build your `syscalls` binary and put the file in `//local/starnix/syscalls`.
(If you are building using the Google-internal build system, be sure to
specific the `--config=android_x86_64` build flag to build an NDK binary.)

You can then build and run your test as usual:

```sh
fx build
fx test syscalls_test
```

### Updating Starnix when running prebuilt tests

When working on the Starnix runner to make behavioral changes that affect
prebuilt tests such as the GVisor tests, re-running the test does not implicitly
fetch and start a new runner. In these cases it is necessary to explicitly stop
the runner so that it is updated on next component launch.

The following snippet performs those stops:

```sh
for moniker in /core/{starnix_runner,starnix_manager,test_manager/{starnix_test_runner,starnix_unit_test_runner}}; do
  ffx component stop -r $moniker
done
```

### Viewing Inspect for debugging

You can view Inspect data exposed by starnix using `ffx inspect`.

To view the thread groups currently running, run:

```
ffx inspect show core/starnix_manager/starbionic:root/galaxy/kernel/thread_groups
```

You can also view the number of syscalls that have been executed:

```
ffx inspect show core/starnix_manager/starbionic:root:syscall_stats
```
