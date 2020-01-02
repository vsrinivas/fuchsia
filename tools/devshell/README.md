# fx subcommands

Sub-commands of `fx` are defined in several directories:

`//tools/devshell` contains core scripts that are part of the supported [fx
workflow](/docs/development/workflows/fx.md).

`//tools/devshell/contrib` contains scripts that have been contributed by
team members that have other levels of support and/or ownership. The OWNERS
file in the contrib directory provides a pointer to the individuals
supporting the scripts there.

`//vendor/*/scripts/devshell` contains scripts that are relevant only to the
particular vendor and will have an ownership and support model documented
there.

Sub-commands can be implemented in a number of languages, but it is
recommended to use `bash` at this time, so as to be able to consume some of
the helpers provided by `//tools/devshell/lib/vars.sh`.

It is recommended that scripts be kept short and simple. Authoring large
shell programs without a significant test plan can lead to hard to maintain
tools. If there is a need to produce a more sophisticated program the
recommended approach is to author a host tool program as part of the regular
Fuchsia build, and only to wrap that program in a very slim way in a script.
Examples of such cases can be found in `fx pave` and `fx make-fuchsia-vol`. A
good rule of thumb here is that if a script only needs to launch and manage a
one or a few sub-processes, then shell may be a fine language. If the program
needs to perform any significant string manipulation or business logic, it is
likely better authored in a language that provides more structural
capabilities and standard library.

## Consuming vars.sh / Implementing Subcommands

Most subcommands start with a pre-amble of this nature (paths vary slightly):

```
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"/../lib/vars.sh || exit $?
fx-config-read
```

This pre-amble ensures that the devshell helpers are loaded, and then reads
the active fx configuration from the user-selected Fuchsia build directory.

`fx-config-read` is required for most environment variables to be set, and is
necessary for most scripts.

## Helper functions

`fx-config-read` loads the current user configuration that is either defined
by the fx configuration modulated by `fx set` and `fx use`, or by flags to fx
such as `--config` and `--dir`. It is necessary as a pre-amble for any script
that wants to consume build artifacts, as it defined most of the standard
environment variables such as `$FUCHSIA_BUILD_DIR`.

`fx-error` and `fx-warn` simply print their arguments, prefixing them with
`ERROR: ` or `WARNING: ` respectively. If the output stream supports color,
then these outputs are colored.

`fx-command-run` and `fx-command-exec` execute another `fx` subcommand, for
example, running `fx-command-run shell` will invoke the `fx shell` command.
The run variant executes the subcommand in a subshell and the exec variant
`exec`s the subcommand, replacing the calling process.

`fx-command-help` prints the command output for the currently running
subcommand.

TODO(raggi): rename the following two functions:

`get-device-name` returns either the device name that the user has set with
`fx set-device` or `fx -d <device-name>`. If the user has not set a default
device, the command will run device discovery, and will return a discovered
device name provided there is only one device discovered.

`get-fuchsia-device-addr` consumes `get-device-name` and returns the Fuchsia
IPv6 address of the device. The returned address is the "netstack address",
not the "netsvc address".

The `vars.sh` script may define additional functions, however, they are
considered internal and may change more often. Users can request additional
helper functions by contacting the devshell owners, or by defining their own
library scripts in contrib.

## Environment Variables

After a successful invocation of `fx-config-read` in a script, one would observe the following environment variables:

```
FUCHSIA_ARCH         - The current architecture selected (currently one of x64/arm64)
FUCHSIA_BUILD_DIR    - The path to the current Fuchsia build directory
FUCHSIA_DIR          - The path to the root of the Fuchsia source tree
FUCHSIA_OUT_DIR      - (deprecated) "$FUCHSIA_DIR/out"
ZIRCON_BUILDROOT     - The path to the Zircon build directory
ZIRCON_TOOLS_DIR     - The path to the Zircon host-tools build directory.
```

`fx-config-read` and/or `fx` could set additional environment variables, but
users should not rely on them - only the above list are to be preserved
(unless marked deprecated).

## Documenting Subcommands

As many `fx` subcommands delegate to sub-programs passing on flags directly
to them, it is prohibitive to always be able to respond to the `-h` or
`--help` flags. As such `fx` subcommands SHOULD implement `-h` and `--help`
if possible, but this is not required.

It is required that all subcommands implement help documentation lines, which
are defined as follows:

```
#### CATEGORY=Category name
### a short one-line (<70 character) description for the command lines
## usage: fx <subcommand> [-a|-b|-c] --foo ...
##
## Long descriptions, flags, and so on
```

The first line starting with `###` is consumed by `fx help` to produce a list
of commands with one-line descriptions of what the command does. These lines
should be kept short so as to render well under `fx help`.

Lines starting with `##` are output when a user invokes `fx help subcommand`,
and are used to provide full command help output. The long form output should
document all flags and provide fuller description of the command behaviors as
appropriate.

Lines starting with `####` contain metadata. The following metadata fields are
supported:

* `#### CATEGORY=Category`: the subcommand is grouped under the specified
  category in the output of `fx help`. There's no enforcement on the name of
  the category, but whenever possible it should be one of the existing
  categories.

* `#### DEPRECATED`: deprecated subcommands are not listed by default on
  `fx help`.

Where possible, a command can use `fx-command-help` to print out the
long-form help (defined by `##` lines). Many commands implement `-h` and
`--help` to invoke `fx-command-help` and this is recommended.

### fx metadata files

When subcommands are scripts, documentation is embedded as comments in
the scripts themselves. However, that's not always possible, for example for
binaries produced by the build, such as `fidldoc`, prebuilt binaries like `gn` and
symlinks like `rustdoc` and `gen-cargo`. In any case where metadata cannot be
in the subcommand itself, `fx` looks for a metadata file with the `.fx` extension
in the same directories where it looks for subcommands. If such a file exists,
it represents a subcommand with the same name without the `.fx` extension.

`<subcommand>.fx` files follow the same format described in
the previous section, with an optional metadata field:

* `#### EXECUTABLE=location_of_executable`: points to the actual executable,
  which can be anywhere in the tree or in the build output. It can/must use the
  following variables to refer to known paths:

  * `${FUCHSIA_DIR}`: root of the Fuchsia source tree
  * `${PREBUILT_3P_DIR}`: location of the 3p prebuilts (usually `${FUCHSIA_DIR}/prebuilt/third_party`)
  * `${HOST_PLATFORM}`: platform of the host, used to compose prebuilt paths
  * `${HOST_TOOLS_DIR}`: path of the host tools produced by the build

  Some examples of valid uses of `EXECUTABLE` in `.fx` files:

  * `#### EXECUTABLE=${FUCHSIA_DIR}/.jiri_root/bin/cipd`
  * `#### EXECUTABLE=${PREBUILT_3P_DIR}/gn/${HOST_PLATFORM}/gn`
  * `#### EXECUTABLE=${FUCHSIA_DIR}/.jiri_root/bin/jiri`
  * `#### EXECUTABLE=${PREBUILT_3P_DIR}/ninja/${HOST_PLATFORM}/ninja`


## Testing

### Testing shell subcommands

Subcommands that are shell scripts should be tested using the Bash test
framework in `//tools/devshell/tests/lib/bash_test_framework.sh`, which provides
facilities for mocking components and encapsulating the execution context in a
temporary directory without any impact on the working tree.

Each test suite with one or more tests is a Bash script which name ends with
`_test` in a subdirectory of `//tools/devshell/tests`.

**To run** shell tests, execute `fx self-test <tests_script>`.
To find out what test scripts are available, run `fx self-test` without
arguments and they will be listed at the bottom. To run all the tests from
all the test scripts, use `--all`. Other sample invocations are described below:

```
fx self-test --all   # run all tests from all tests scripts
fx self-test subcommands    # run all tests scripts in //tools/devshell/tests/subcommands
fx self-test subcommands/fx_set_test   # run all tests in //tools/devshell/tests/subcommands/fx_set_test
fx self-test fx-internal/fx_test   # run all tests in //tools/devshell/tests/fx-internal/fx_test
fx self-test fx-internal/fx_test --test TEST_fx-subcommand-run   # run a single test from fx-internal/fx_test
```

**To implement** new shell test scripts, create a new file `*_test` in a
subdirectory of `//tools/devshell/tests` using the Bash test framework
documented in `//tools/devshell/tests/lib/bash_test_framework.sh`.

There are many examples in [`//tools/devshell/tests`](tests/). The test
framework is documented in the [framework script](tests/lib/bash_test_framework.sh).


### Testing non-shell subcommands

Subcommands that are primarily non-shell, for example Rust or Dart, should
have regular tests integrated with the Fuchsia build.

For example, the `fx test` subcommand is written in Dart and has tests
defined in its [BUILD.gn](/scripts/fxtest/BUILD.gn) file.

