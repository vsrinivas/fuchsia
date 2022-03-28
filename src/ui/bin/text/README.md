# text_manager

The `text_manager` is a program that serves the text editing needs of Fuchsia
clients.


## Building

To build the text manager, make sure you have `--with=//src/ui/bin/text` in your
`fx set` command.

Then, either `fx build`, or a more targeted:

```
fx build src/ui/bin/text
```

will build the needed components.

## Testing

### Prerequisites

* You will need either an emulator or a physical Fuchsia device attached.
* You need to have a `fx serve` running in a separate terminal, and need to
  ensure it connects correctly to the device or emulator.

### Running the tests

To run the text manager tests, make sure you have
`--with=//src/ui/bin/text:tests` in your `fx set` command.

Then you can type:

```
fx test //src/ui/bin/text
```

to run all the tests below the named directory.

## Documentation

For internals documentation refer to the [`docs/` folder](docs/).
