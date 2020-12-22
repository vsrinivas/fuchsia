# ansi-junk

A proof-of-concept demonstration of how ANSI escape codes can be "leaked" by logging tools across
multiple messages when rendered with `log_listener` in a terminal..

## Building

To add this component to your build, append
`--with examples/diagnostics/logs/ansi-junk`
to the `fx set` invocation.

## Running

```
$ fx shell run fuchsia-pkg://fuchsia.com/ansi-junk#meta/ansi-junk.cmx
```

## Testing

Unit tests for ansi-junk are available in the `ansi-junk-tests`
package.

```
$ fx test ansi-junk-tests
```
