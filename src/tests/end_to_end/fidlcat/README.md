# fidlcat_test README

### What is this test about?

This is a set of tests for the host tool fidlcat.  fidlcat is a host
tool that communicates with a debug agent on the target.  The debug
agent monitors a target process for FIDL-related behavior (mostly
sending and receiving FIDL messages) and reports it back to fidlcat.

Because fidlcat communicates with the target, we need to write an e2e
test to exercise that interaction.

Relevant code paths with unit and integration tests:

```
//tools/fidlcat
//src/lib/fidl_codec
//src/developer/debug/debug_agent
```

See the [fidlcat
docs](https://fuchsia.dev/fuchsia-src/development/monitor/fidlcat) for
more information about fidlcat.

### How to run this test manually

1. Add `//src/tests/end_to_end/fidlcat:tests` to your `universe_package_labels` and `fx build`.
2. Run `fx test --e2e fidlcat_e2e_tests`.
