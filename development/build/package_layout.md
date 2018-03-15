# Layout of package directories

Each [layer](/development/source_code/layers.md) of the Fuchsia source tree
contains a top-level directory called `package` containing all the
[build packages](packages.md) for that layer. The present document describes
the structure of these directories.

## Directory map

In the diagram below, "pkg" refers to Fuchsia packages, the unit of installation
in Fuchsia.

```
//<layer>/packages
    <layer>          # all production pkg up to this layer
    dev              # pkg declared at this layer; for daily development
    default          # alias for dev
    dev_full         # all pkg up to this layer
    all              # grab bag of every pkg in this layer
    prod/            # pkg that can be picked up in production
    tests/           # correctness tests (target & host)
    tools/           # dev tools not for prod (target & host)
    benchmarks/      # performance tests
    examples/        # pkg demonstrating features offered by this layer
    experimental/    # pkg not quite ready for prod
  * config/          # config files for the system (e.g. what to boot into)
  * sdk/             # SDK definitions
  * products/        # definitions for specific products
```

## Cross-layer dependencies

- `<layer>(N)` depends on `<layer>(N-1)` and adds all the production artifacts
  of (N)
  - this defines a pure production build
- `dev(N)` depends on `<layer>(N-1)` and adds all artifacts of (N)
  - this defines a build suitable for developing (N) itself
- `dev_full(N)` depends on `dev_full(N-1)` and adds all artifacts of (N)
  - this defines a build suitable for developing (N) as well as its dependencies

## Inner-layer dependencies

Most directories in a `packages` directory contain a special `all` package which
aggregates all packages in this directory. Every `all` package should roll up to
the root `all` package, thereby creating a convenient shortcut to build "all
packages in the layer".
Note that the directories that do not require aggregation are marked with `*` in
the diagram above.

`default` is currently an alias for `dev`.

## Verification

The [`//scripts/packages/verify_layer`][verify-layer] tool is used to verify
that a layer's `packages` directory's structure matches the description in the
present document.


[verify-layer]: https://fuchsia.googlesource.com/scripts/+/master/packages/README.md
