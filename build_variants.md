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

Some shorthands for common cases are provided by the `gen.py` script that
is the usual way to run `gn gen` in the Fuchsia build, via `--variant`
switches.  Each `--variant` switch adds one matching rule.

Here's an example running `gen.py` directly:

```sh
./build/gn/gen.py --variant=host_asan --variant=asan=cat,ledger
```

This does the same thing using the `fx set` tool:

```sh
fx set x86 --variant host_asan --variant asan=cat,ledger
```

 1. The first switch applies the `host_asan` matching rule, which enables
    [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
    for all the executables built to run on the build host.

 2. The second switch applies the `asan` matching rule, which enables
    AddressSanitizer for executables built to run on the target (i.e. the
    Fuchsia device).  The `=...` suffix constrains this matching rule only
    to the binaries named `cat` and `ledger`.

The switch `--variant=help` will make `gen.py` show the list of recognized
shorthands, and which ones can work with the `=...` suffix.

The GN code supports much more flexible matching rules than just the binary
name, but there are no shorthands for those.  To do something more complex,
set the `select_variant` GN build argument directly.

 * You can do this via the `--args` switch to `gen.py` once you have the
   syntax down.

 * The easiest way to experiment is to start with some `--variant` switches that
   approximate what you want and then edit the `select_variant` value `gen.py`
   produces:
   * You can just edit the `args.gn` file in the GN output directory
     (e.g. `out/debug-x86-64/args.gn`) and the next `ninja` run (aka `fx build`)
     will re-run `gn gen` with those changes.
   * You can use the command `./buildtools gn args out/debug-x86-64`, which
     will run your `$EDITOR` on the `args.gn` file and then do `gn gen`
     immediately so you can see any errors in your GN syntax.

Like all GN build arguments, `select_variant` is documented in comments in
the `.gn` source file that defines the argument, and these can be displayed
using `./buildtools/gn args --list`.  The `--help-args` switch to `gen.py`
also runs GN this way:

```sh
./build/gn/gen.py -p topaz/packages/default --help-args=select_variant
```

You can also use the `fx set` tool to run `gen.py` for you:

```sh
fx set x86 --help-args select_variant
```

If you combine this with one or more `--variant` switches, GN will show you
the actual value of `select_variant` that `gen.py` chose.  For example:

```sh
./build/gn/gen.py -p topaz/packages/default --help-args=select_variant --variant=asan=cat,ledger
```

or:

```sh
fx set x86 --help-args select_variant --variant asan=cat,ledger
```

prints out:

```
select_variant
    Current value = [{
  output_name = ["cat", "ledger"]
  variant = "asan"
  host = false
}]
      From :1
    Overridden from the default = []
...
```

To see the list of variants available and learn more about how to define
knew ones, use `--help-args known_variants` with `gen.py` or `fx set`
(or `gn args out/... --list=known_variants`).
