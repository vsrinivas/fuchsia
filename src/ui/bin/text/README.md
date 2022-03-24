# Text manager

The text manager is Fuchsia's provider of text-related FIDL services.

Check out the [docs/][docs] directory for the documentation of the text manager
internals.

## Building

Ensure that you have `--with=//src/u/bin/text` in your `fx set ...` command
line.

Then, `fx build` or `fx build src/ui/bin/text` will build all the production
text manager code that you may need.


## Testing

Ensure that you have `--with=//src/u/bin/text:tests` in your `fx set ...`
command line.

Run the tests with:

```
fx test //src/ui/bin/text
```

This test invocation will compile the test dependencies and run any tests that
are in this directory and its subdirs.

[docs]: docs/README.md
