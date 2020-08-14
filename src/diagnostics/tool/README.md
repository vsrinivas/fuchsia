# Diagnostics Tool

Reviewed on: 2020-04-15

The Diagnostics Tool is a utility for interacting with diagnostics data on a
Fuchsia system.

The Diagnostics Platform exposes filtered views of all diagnostics data on a
system, subject to the constraints in selector files. This tool currently
provides an interactive terminal UI to aid writing the selector configuration.

## Building

To add this project to your build, append `--with
//src/diagnostics/tool:diag_tool_host` to the `fx set` invocation.

For example:

```
fx set core.chromebook-x64 --with //src/diagnostics/tool:diag_tool_host
```

After building the tool binary, you will be able to execute it from your Fuchsia
project root directory at: `./out/default/host-tools/diag_tool`.

## Running

### Prerequisites

In order to run this tool, you will first require a json dump of Inspect data.

You can obtain the initial dump through fx snapshot.

While connected to a running Fuchsia device, run the following commands:

```
fx snapshot -o <path/to/snapshot/dump>
unzip <path/to/snapshot/dump>/snapshot.zip -d <path/to/snapshot/dump/contents>
```

Now, under `<path/to/snapshot/dump/contents>`, you will have a file named
`inspect.json` which contains a json serialization of all inspect data on the
system.

### Generating Selectors

*NOTE: This section assumes a json file at
`<path/to/snapshot/dump/contents>/inspect.json` exists which includes the json
serialized inspect hierarchy for the reader's component.*

The diagnostics tool helps clients define their selector configuration files by
auto-generating a base-file explicitly including all diagnostics data the client
exposes.

Running the following command will produce a large file of all explicit selector
strings for every diagnostics property found in `<component_name>.cmx`'s
hierarchy within the provided inspect.json file. This large list acts as the
starting point for your integration.

```
./out/default/host-tools/diag_tool -b <path/to/snapshot/dump/contents>/inspect.json generate -c <component_name>.cmx <selectors.cfg>
```

The output file is `<selectors.cfg>`, which will be used in the next section.

### Interactively Applying Selectors

Once you have your initial file of explicit selectors from the Generating
Selectors section above, it's time to interactively start refining the list. In
one terminal pane, open the selector file in your prefered editor. In a second
pane, run the following command:

```
./out/default/host-tools/diag_tool -b <path/to/snapshot/dump/contents>/inspect.json apply -c <component_name>.cmx <selectors.cfg>
```

In the pane where the above command was run, an interactive session will open
which shows the `<component_name>.cmx` hierarchy as filtered by the selectors in
`<selectors.cfg>`. By default, the hierarchy should be fully present, and
missing data will appear as RED text in the window.

This interactive session has 3 important keys:

*   (Q) will exist the interactive session.
*   (H) will collapse all RED, missing data and only display the explicit
    hierarchy being selected for by your configuration file.
*   (R) will refresh your interactive session. You may edit `<selectors.cfg>` in
    a seprarate window and press R to apply the edited selectors to the session.

## Testing

To run unit tests:

```
fx set ... --with //src/diagnostics/tool:diag_tool_tests
fx run-test inspect_validator_tests
```
