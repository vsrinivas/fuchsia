Fxfs is currently an experimental filesystem that might end up going nowhere. It
is currently being used to explore ideas and may end up being the basis of a
Minfs replacement. If and when that happens, RFCs will cover the design.

Fxfs has rustdoc style documentation which can be generated and opened with
command like

```
fx rustdoc //src/storage/fxfs:lib --open
```

Note: The last line prints the documentation location, which depends on target.
