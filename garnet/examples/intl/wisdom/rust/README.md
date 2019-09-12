# Wisdom server in Rust

This is a Rust variant of the wisdom server.   The wisdom server integrates
with the service `fuchsia.intl.Profile` to serve a client query to represent
a date-time point in several different calendars.

# Setup

To compile this program you will need to add the following flag to your `fx set`
invocation:

```
--with=//garnet/examples/intl/wisdom/rust:tests
```

While this has a wider scope than just the binary, it also compiles the package
`intl_wisdom_rust` which you can see in the [BUILD.gn file](BUILD.gn).

# Compiling

*This section requires "Setup" (see above).*

If you have configured your `fx set` command properly (see previous section),
you should be able to compile like so:

```bash
fx build examples/intl/wisdom/rust:tests
```

# Running

*This section requires "Setup" (see above).*

To run the example, you will need to build first with:

```
fx build
```

Thereafter in a separate terminal you need:

```
fx serve -v
```

Once both are done (and `fx serve` is still running), the following will run
the program on the currently set device:

```
fx shell run \
  fuchsia-pkg://fuchsia.com/intl_wisdom_rust#meta/intl_wisdom_server_rust.cmx
```

# Testing

Make sure that you have ran `fx build` and that `fx serve` is running.

Then you can run tests like so:

```
fx run-tests intl_wisdom_server_rust_tests
```

# Troubleshooting

Try checking whether the package is available on the device:

```bash
$ fx shell ls /pkgfs/packages  | grep wisdom_rust
intl_wisdom_rust
```

