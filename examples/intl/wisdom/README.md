# Internationalization Example: Intl Wisdom

Simple example of using `fuchsia.intl.Profile` (from `intl.fidl`) for conveying
internationalization properties to services. The wisdom server integrates
with `fuchsia.intl.Profile` to serve a client query to represent a date-time
point in several different calendars.

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples --with //examples:tests
$ fx build
```

If you do not already have one running, start a package server so the example
components can be resolved from your device:

```bash
$ fx serve
```

## Running

To run the one of the examples defined here, provide the full component URL to
`run` to create the component instances inside a restricted realm for
development purposes:

-  **C++**

    ```bash
    $ ffx component run 'fuchsia-pkg://fuchsia.com/intl_wisdom#meta/intl_wisdom_realm.cm'
    ```

-  **Rust**

    ```bash
    $ ffx component run 'fuchsia-pkg://fuchsia.com/intl_wisdom_rust#meta/intl_wisdom_realm.cm'
    ```

This creates the client and server instances, routing the necessary capabilities.
To start the wisdom client, use the following command:

```bash
$ ffx component bind /core/ffx-laboratory:intl_wisdom_realm/wisdom_client
```

This starts the server component automatically. You can see the client output
using `fx log`.

## Testing

To run the test components defined here, provide the build target to
`fx test`:

```bash
$ fx test //examples/intl/wisdom
```

## Options

You can optionally specify the following using `program` arguments in the client
manifest.

```json5
args: [
    "--timestamp=2018-11-01T12:34:56Z",
    "--timezone=America/Los_Angeles"
]
```

**timestamp**

Timestamp in ISO-8601 format, to be used in generating bits of wisdom.

**timezone**

Timezone that will be set in the `Profile`, by using an
[IANA Time Zone ID](http://en.wikipedia.org/wiki/List_of_tz_database_time_zones#List).
