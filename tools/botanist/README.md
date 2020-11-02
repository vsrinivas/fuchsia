# botanist

**botanist** is a tool used by the infrastructure to set up the bot environment
before running tests. It handles setting some environment variables used by the
other tools it invokes and also handles the initial starting up of the target(s)
it will run the tests on.

botanist currently has three subcommands, two of which are planned for
deprecation:

*   `run`
`botanist run` is the main subcommand which is used by most of the builders run
by the infrastructure. It takes a device config and image manifest and starts up
the corresponding target in the device config. It also takes a command to run
after starting up the targets. Usually this will be
[testrunner](https://fuchsia.googlesource.com/fuchsia/+/HEAD/tools/testing/testrunner)
which takes in a test manifest containing the tests to run.

*   `qemu`
`botanist qemu` is used to run bringup tests on QEMU. It starts a QEMU instance
with a config it constructs from the command arguments and waits for it to
complete. The required arguments are `-type` which specifies what type of
emulator to run (`qemu` or `aemu`), and `-qemu-dir` which points to a directory
containing the qemu binaries. It writes the test results to a MinFS image
provided by the `-minfs` flag.

*   `zedboot`
`botanist zedboot` is used to run bringup tests on physical devices. It paves the
device and polls for a summary.json to be produced that contains the results to
the tests that were run. This summary.json follows the `runtests.TestSummary`
schema.

<!-- TODO(fxbug.dev/59963): `qemu` and `zedboot` subcommands will be deprecated
once bringup tests are running over serial with testrunner using the `run`
subcommand. -->

`qemu` and `zedboot` are both used for bringup tests. They take in an image
manifest and various args to define the device config and where to output
results. The positional arguments are used as extra command-line arguments to
pass to the kernel.

The infrastructure writes a [runcmds
script](https://fuchsia.googlesource.com/infra/recipes/+/ee760615bf68313e51ce11efaa8e5a414ba1db80/recipe_modules/testing_requests/api.py#847)
which contains a list of all the tests it wants to run and embeds this into the
zbi image used to boot the target. It then calls one of these subcommands with a
command line argument that specifies to run the runcmds script on boot. After
botanist starts up the target, it waits until it gets a signal that the tests
are completed.
