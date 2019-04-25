# Fuchsia Build System: Variants

The Fuchsia GN build machinery allows for separate components to be built
in different "variants".  A variant usually just means using extra compiler
options, but they can do more than that if you write some more GN code.
The variants defined so far enable things like
[sanitizers](https://github.com/google/sanitizers/wiki) and
[LTO](https://llvm.org/docs/LinkTimeOptimization.html).

The GN build argument
[`select_variant`](/docs/gen/build_arguments.md#select_variant)
controls which components are built in which variants.  It applies
automatically to every `executable`, `loadable_module`, or `driver_module`
target in GN files.  It's a flexible mechanism in which you give a list of
matching rules to apply to each target to decide which variant to use (if
any).  To support this flexibility, the value for `select_variant` uses a
detailed GN syntax.  For simple cases, this can just be a list of strings.

Using `fx set`:

```sh
fx set core.x64 --variant={host_asan,asan/cat,asan/ledger}
```

Alternatively, you can add or modify the variants on an existing build by
editing the GN args (substituting your build's GN output directory
for `out/default` as necessary):

```sh
./buildtools/gn args out/default
```

That command will bring up an editor. Append to that file:

```
select_variant = [ "host_asan", "asan/cat", "asan/ledger" ]
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
name, but there are no shorthands for those. See the
[`select_variant`](/docs/gen/build_arguments.md#select_variant)
build argument documentation for more details.

To see the list of variants available and learn more about how to define
new ones, see the
[`known_variants`](/docs/gen/build_arguments.md#known_variants)
build argument.
