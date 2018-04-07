# Fuchsia Build System: Variants

The Fuchsia GN build machinery allows for separate components to be built
in different "variants".  A variant usually just means using extra compiler
options, but they can do more than that if you write some more GN code.
The variants defined so far enable things like
[sanitizers](https://github.com/google/sanitizers/wiki) and
[LTO](https://llvm.org/docs/LinkTimeOptimization.html).

The GN build argument `select_variant` controls which components are built
in which variants.  It applies automatically to every `executable`,
`loadable_module`, or `driver_module` target in GN files.  It's a flexible
mechanism in which you give a list of matching rules to apply to each
target to decide which variant to use (if any).  To support this
flexibility, the value for `select_variant` uses a detailed GN syntax.
For simple cases, this can just be a list of strings.

Here's an example running `gn gen` directly:

```sh
./buildtools/gn gen out/x64 --args='select_variant=["host_asan", "asan/cat", "asan/ledger"]'
```

This does the same thing using the `fx set` tool:

```sh
fx set x64 --variant={host_asan,asan/cat,asan/ledger}
```

 1. The first switch applies the `host_asan` matching rule, which enables
    [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
    for all the executables built to run on the build host.

 2. The second switch applies the `asan` matching rule, which enables
    AddressSanitizer for executables built to run on the target (i.e. the
    Fuchsia device).  The `/cat` suffix constrains this matching rule only
    to the binary named `cat`.

 3. The third switch is like the second, but matches the binary named `ledger`.

The GN code supports much more flexible matching rules than just the binary
name, but there are no shorthands for those.  To do something more complex,
set the `select_variant` GN build argument directly.

 * You can do this via the `--args` switch to `gn gen` once you have the
   syntax down.

 * The easiest way to experiment is to start with some `--variant` switches that
   approximate what you want and then edit the `select_variant` value `fx set`
   produces:
   * You can just edit the `args.gn` file in the GN output directory
     (e.g. `out/x64/args.gn`) and the next `ninja` run (aka `fx build`)
     will re-run `gn gen` with those changes.
   * You can use the command `./buildtools gn args out/x64`, which
     will run your `$EDITOR` on the `args.gn` file and then do `gn gen`
     immediately so you can see any errors in your GN syntax.

Like all GN build arguments, `select_variant` is documented in comments in
the `.gn` source file that defines the argument, and these can be displayed
using `./buildtools/gn args --list`.  The `--help-args` switch to `fx set`
also runs GN this way:

```sh
./buildtools/gn args out/x64 --list=select_variant
```

You can also use the `fx set` tool to run `gn args` for you:

```sh
fx set x64 --help-args=select_variant
```

To see the list of variants available and learn more about how to define
knew ones, use `--help-args=known_variants` or `fx set`
(or `gn args out/... --list=known_variants`).
