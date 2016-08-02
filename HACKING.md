Hacking on Modular
==================

[![Build Status](https://travis-ci.com/domokit/modular.svg?token=wsf45jVHk4mEroJSmL79&branch=master)](https://magnum.travis-ci.com/domokit/modular)

## Initial setup

Modular supports building for Android from a Linux or Mac host as well as
building for Linux. Building for Mac isn't yet supported.

1.  Configure [SSH authentication with your GitHub account](https://help.github.com/articles/generating-a-new-ssh-key-and-adding-it-to-the-ssh-agent/#adding-your-ssh-key-to-the-ssh-agent).
    Then download the repo:
    ```sh
    git clone git@github.com:domokit/modular.git
    ```
    `cd modular; export MODULAR_HOME=${PWD}`

1.  Download [Android Studio](https://developer.android.com/studio/index.html)
    for your platform and make sure the `adb` tool is in your path.

1.  **Recommended:** Building, running and testing may be driven by the `modular`
    command. To use it:
    ```sh
    export PATH="${MODULAR_HOME}/bin:${PATH}"
    ```

    See `modular help` for full usage.

1.  **Recommended:** If your editor is integrated with dart tools, then point
    its SDK at the same one modular uses. This ensures the behavior of the analyzer and formatter are consistent with the command line tools.
    ```sh
    ${MODULAR_HOME}/third_party/flutter/bin/cache/dart-sdk
    ```

1.  Modular uses Chromium's
    [depot_tools](https://www.chromium.org/developers/how-tos/depottools)
    to ease code review and deploy built assets. If you don't have it already,
    get it and put it on your PATH (consider configuring your shell to do so by
    default).
    ```sh
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
    export PATH="`pwd`/depot_tools:${PATH}"
    ```

### Android device setup

To run Modular, an Android device running Lollipop 5.0 or greater has to be
connected via USB and have **[USB debugging
enabled](http://developer.android.com/tools/device.html#setting-up)**.

**Optional:** To debug Modular, the device must also be
**[rooted](https://source.android.com/source/building-devices.html#unlocking-the-bootloader)**.

## Development cycle

### Syncing

A `git pull` updates to tip of tree. Running the `modular` command will make
sure your dependencies are in sync.

### Building

Use the `modular` tool to build. Here are some example invocations:
```sh
# Build for android (in debug mode default).
modular build --target=android

# Build for linux in release mode.
modular build --release
```

### Re-generating mojom-dart bindings

This is required if any .mojom file has been modified.
```sh
modular gen
```

### Testing

Modular has three kinds of tests: dart unittests, mojo apptests and gtest binary
tests. To run all tests the same way the continous build does:
```sh
modular test
```

To add a new test to this suite, add it to the appropriate file in
`modular_tools/data/`.

For a faster development cycle, tests may be run individually without the
`modular` tool.

#### Dart unittests

Move to the directory containing the test and invoke them via `pub`.
For example:
```sh
cd parser/
pub run test
```

### Running

You may build and run the default recipe via the `modular` tool as such:
```sh
# Build and run for android in release mode.
modular run --target=android --release
```

#### Running an existing session, recipe or module

By default, `run` command runs the `examples/recipes/launcher.yaml` recipe
which shows the selected user's home screen.

To create a new session instance for an arbitrary recipe:
```sh
modular run --recipe <SRC_RELATIVE_RECIPE_PATH>
```

Each time a new session is created, its identifier is printed to stdout (look
below the "Modular" ascii art banner). To restore an existing session instance,
pass its session id:
```sh
modular run --session <SESSION_ID>
```

To run a recipe directly, follow the build instructions above, then run it via
`mojo_run`. For example:
```sh
third_party/mojo_devtools/mojo_run examples/recipes/competition.yaml
```

To run just one module do:
```sh
modular run --module <MODULE_URL>
```
where the module URL comes from the `manifest.json`. This will generate a
session containing just that module. If the module has required inputs it won't
run but it will print
an explanation of why.

You can pre-populate the session graph by passing `--session-data=<JSON>` to
`modular run`. The JSON you pass takes the form of a set of nested JSON
objects. Each object represents a node, each of its fields is an outgoing edge.
The field name is a space separated list of edge labels. Edge labels can be
abbreviated by using shorthand specified in the session's recipe's `use:`
section. The root JSON object is the root session node. JSON string literals
are interpreted as string values. JSON numbers are interpreted as integer
values. For example the nutritional info module can be run with:
```
modular run --module https://tq.mojoapps.io/nutritional_info.mojo \
    '--session-data={"food":"lasagna"}'
```
*Note: you may need to quote or escape JSON data in your shell*

To see what's happening in the session graph as your module or recipe runs pass
`--watch`. A JSON serialization of the whole session graph will be printed to
the log.

#### Source for the Mojo dependencies

By default the `run` script configures the Mojo shell to resolve 'mojo:' urls to
app binaries uploaded to the cloud, built against the same version of the Mojo
SDK that your checkout of Modular is using. You can alter the source of Mojo app
binaries by passing the `--mojo-origin` flag to `run`.

To run against the latest app versions in the CDN:
```sh
modular run --target=android --mojo-origin=https://core.mojoapps.io/
```

To run using locally built apps:
```sh
modular run --target=android --mojo-origin=/path/to/mojo/src/out/android_Debug
```

#### Source for the Flutter engine

You can use a locally built version of the [Flutter
engine](https://github.com/flutter/engine):

To run using locally built apps:

```sh
modular run --local-flutter-engine=/path/to/engine/src/out/Debug
```

or

```sh
modular run --local-flutter-engine=/path/to/engine/src/out/android_Debug \
  --target=android
```

Note that this does not affect the Flutter SDK.

**IMPORTANT: VERSION COMPATIBILITY**

When using these flags, it is easy to try to run a configuration that doesn't
work. To help ensure compatibility:

1.  It's safest to use either all release or all debug artifacts. Keep in mind
    the shell and services on the CDN are release-only.

1.  Modular is only guaranteed to work with the Mojo shell and services built at
    the version of Mojo stored in the [MOJO_VERSION](MOJO_VERSION) file.

#### Running and visualizing a recipe without Mojo

See [simulator](simulator/README.md) for instructions on how to emulate a
session and visualize its recipe and session graph.

### Debugging

#### Inspector
The Inspector can be used to observe the state of the handler: the composition
tree, active sessions, etc. See [inspector/README.md](inspector/README.md) for
more information and instructions.

#### Dart Observatory
Both Dart and Flutter content handler expose their instances of the [Dart
Observatory](https://dart-lang.github.io/observatory/). Typically during a
Modular run there is a number of Mojo applications running in the Dart content
handler (pure dart modules, handler), and a number of Mojo applications running
in the Flutter content handler (Flutter modules) - we need to use the
appropriate observatory depending on what we want to inspect.

To access the observatories when running on **Linux**, run Modular, look for
printouts like these and open the url in a browser:

```
Observatory listening on http://127.0.0.1:48955
```

When running tethered on **Android**, you will see the same printouts, but
addresses are local to the device.  Wait for mojo devtools to set up the
forwarding and print a line like this, typically a few seconds after Modular
starts:

```
Dart observatory available at the host at http://127.0.0.1:13776
```

### Tracing

#### Annotating the code

Use `dart:developer` timeline support to annotate the methods that you want to
trace.

#### Recording startup traces

Run Modular as follows:

```
modular run --release --trace-startup
```

After 10s (can be configured via `--trace-startup-duration` flag), the shell
will write the startup trace to a file, and report that in the stdout:

```
[INFO:tracer.cc(114)] Wrote trace data to
/data/user/0/org.chromium.mojo.shell/cache/tmp/mojo_shell.trace
```

If running on Android as in the example above, the shell saves the trace to a
file on the device. Use the following command to pull down the file:

```sh
adb shell run-as org.chromium.mojo.shell cat \
/data/user/0/org.chromium.mojo.shell/cache/tmp/mojo_shell.trace \
> mojo_shell.trace
```

The resulting file can be opened in chrome://tracing.

#### Recording traces in mojo:tracing

You can also start and stop the tracing interactively. For that, run Modular as
follows:

```sh
modular run --release --debugger
```

and then, in separate shell:

```sh
third_party/mojo_devtools/mojo_debug tracing start
third_party/mojo_devtools/mojo_debug tracing stop ~/output_file
```

This will contain events from the Dart content handler, mojo shell, network
service, and every other service that participates in the mojo tracing
ecosystem. Flutter content handler does not register their events, so traces
from Flutter modules are not be represented.

#### Recording traces in Dart Observatory

You can also record traces in Dart Observatory. These are the easiest to
collect, but each observatory exposes only the trace events happening within the
corresponding content handler. To record traces this way, open the observatory,
click on "timeline", and there "Start recording", then "Stop recording" and
finally "Refresh".

## Deploying

### Build artifacts

Modular's continuous build deploys to a CDN after each green build.
A stable version is deployed at `https://tq.mojoapps.io` manually.

1.  Trigger an Android release build of Modular by `modular build --release
    --target=android`.

1.  Deploy the locally built binaries via `./modular_tools/deploy --release
    --android --stable`.
The stable deployed version can be run on any Android device with a
`@google.com` account. To do so:
```sh
modular run --target=android --deployed
```

## Updating Dependencies

Modular's dependencies are described as git revisions in `*_VERSION` files
read by the `bin/modular` script. There is a script to update the Flutter and
Mojo dependencies:
    ```sh
    ./modular_tools/update-flutter-mojo-deps.py
    ```
By default it rolls to the `alpha` branch of flutter and `master` of
mojo_devtools.
