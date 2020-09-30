# fx subcommands

Subcommands of `fx` are defined in several directories:

`//tools/devshell` contains core scripts that are part of
[fx workflow](/docs/development/build/fx.md).

`//tools/devshell/contrib` contains scripts that have been contributed by
project members that have other levels of support, ownership, or both. The
OWNERS file in the contrib directory provides a pointer to the individuals
supporting the scripts there.

`//vendor/*/scripts/devshell` contains scripts that are relevant only to the
particular vendor and will have an ownership and support model documented
there.

Subcommands can be implemented in a number of languages, but it is
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

## Consuming vars.sh and implementing subcommands

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
`exec`s the subcommand, replacing the calling process. They both set a variable
`FX_CALLER` to the caller script, which can be useful to switch behavior if
the user executed the script directly or if it was executed by another fx
script.

`fx-command-help` prints the command output for the currently running
subcommand.

`fx-export-device-address` resolves the current device as currently
configured by set-device, or detected via `device-finder` and
exports:

* `FX_DEVICE_NAME` the device name as either set using `-d`, `fx
  set-device`, or resolved by search.
* `FX_DEVICE_ADDR` the device fuchsia address as resolved.
* `FX_SSH_ADDR` the device address formatted as required for `ssh(1)`.
  (IPv6 includes `[]`).
* `FX_SSH_PORT` the device SSH port if set via `fx set-device`.

`get-device-name` returns either the device name that the user has set with
`fx set-device` or `fx -d <device-name>`. If the user has not set a default
device, the command will run device discovery, and will return a discovered
device name provided there is only one device discovered.

`get-fuchsia-device-addr` consumes `get-device-name` and returns the Fuchsia
address of the device. The returned address is the "netstack address", not
the "netsvc address".

`get-device-addr-resource` is the same as `get-fuchsia-device-addr`, except
that it ensures the address is correctly formatted for use by tools such as
`ssh(1)`, i.e. for IPv6 addresses, the address is encased in `[]`.

`get-device-addr-url` is the same as `get-device-addr-resource` except that
it ensures that IPv6 scopes are appropriately percent-encoded.

`fx-device-finder` invokes the `device-finder` program with the given
arguments. It reports an error to the user and exits if the program is not
present in the build output. Most scripts should prefer one of the
aforementioned `get-*` functions to perform related operations instead, as
`fx-device-finder` usage will not be `fx set-device` aware.

The `vars.sh` script may define additional functions, however, they are
considered internal and may change more often. Users can request additional
helper functions by contacting the devshell owners, or by defining their own
library scripts in contrib.

## Environment variables

After a successful invocation of `fx-config-read` in a script, one would observe the following environment variables:

```
FUCHSIA_ARCH         - The current architecture selected (currently one of x64/arm64)
FUCHSIA_DIR          - The path to the root of the Fuchsia source tree
FUCHSIA_BUILD_DIR    - The path to the current Fuchsia build directory
HOST_OUT_DIR         - The path to the Fuchsia host-tools build directory
                         (usually $FUCHSIA_BUILD_DIR/host_$HOST_ARCH)
ZIRCON_BUILDROOT     - The path to the Zircon build directory
ZIRCON_TOOLS_DIR     - The path to the Zircon host-tools build directory.
FUCHSIA_OUT_DIR      - (deprecated) "$FUCHSIA_DIR/out"
```

`fx-config-read` and/or `fx` could set additional environment variables, but
users should not rely on them - only the above list are to be preserved
(unless marked deprecated).

## Optional features

`fx` supports the definition of optional features that are enabled by default
and can be disabled by the user for the duration of a single `fx` invocation.

These features can be used during the transition phase of Large Scale Changes
that span across multiple commands. The potentially disruptive changes can be
guarded behind an optional feature, so that users can be quickly unblocked by
disabling the feature themselves.

Features have unique labels and shell commands can check if the given feature is
enabled by using the `is_feature_enabled` method in
[//tools/devshell/lib/fx-optional-features.sh](lib/fx-optional-features.sh).

By default all optional features are enabled. If the user explicitly calls
`fx --disable=FEATURE ...`, the feature is considered disabled for the duration
of that call.

When the flag `--disable=<FEATURE>` is used in a `fx` call, `fx` exports an
environmental variable named `FUCHSIA_DISABLED_<FEATURE>`, so all commands
by default inherit it. Shell commands can verify if a feature is enabled by
using the helper methods in [fx-optional-features.sh](lib/fx-optional-features.sh),
but non-shell commands, like Dart, can directly check for the value of the
environmental variable. If `FUCHSIA_DISABLED_<FEATURE>` is set to "1",
"FEATURE" is disabled, otherwise it is enabled.

## Documenting subcommands

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
of subcommands with one-line descriptions of what the subcommand does.
These lines should be kept short so as to render well under `fx help`.

Lines starting with `##` are output when a user invokes `fx help subcommand`,
and are used to provide full subcommand help output. The long form output should
document all flags and provide fuller description of the subcommand behaviors as
appropriate.

Lines starting with `####` contain metadata. The following metadata fields are
supported:

* `#### CATEGORY=Category`: the subcommand is grouped under the specified
  category in the output of `fx help`. There's no enforcement on the name of
  the category, but whenever possible it should be one of the existing
  categories.

* `#### DEPRECATED`: deprecated subcommands are not listed by default on
  `fx help`.

Where possible, a subcommand can use `fx-command-help` to print out the
long-form help (defined by `##` lines). Many subcommands implement `-h` and
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

## Default device SSH keys

When you check out the Fuchsia tree, a jiri hook generates a default set of SSH credentials on your machine. These are used to pave Fuchsia devices and to connect to devices during development. You can find these keys stored in your home directory by default:

* SSH private key (identity file): `~/.ssh/fuchsia_ed25519`
* SSH public key: `~/.ssh/fuchsia_ed25519.pub`
* List of public keys that allow connection to the Fuchsia device: `~/.ssh/fuchsia_authorized_keys`

These paths are stored in a manifest in the root of your Fuchsia checkout. This file contains the _absolute_ path to the Fuchsia private key and the _absolute_ path to the Fuchsia-specific authorized_keys file, one on each line.

* Manifest: `//.fx-ssh-path`

Previously, these credentials were stored inside the Fuchsia tree as `//.ssh/pkey`, `//.ssh/pkey.pub`, and `//.ssh/authorized_keys`. If SSH credentials are found in both locations and don't match, jiri update will fail. If you find yourself in this situation, remove one of the sets of keys and run jiri run-hooks.
