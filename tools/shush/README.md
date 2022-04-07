# Shush

This tool consumes json diagnostics emitted by the rust compiler (and clippy), and tries to address them by either inserting `#[allow(...)]` annotations or applying compiler-suggested fixes to the code automatically. It can be used to make large scale changes to roll out new clippy lints, perform edition migration, and address new compiler warnings from upstream.

# Usage:

``` sh
# Allow a specific lint or category
fx clippy -f <source file> --raw | shush --lint clippy::suspicious_splitn allow

# See all clippy lints in our tree
fx clippy --all --raw | shush --lint clippy::all --dryrun allow

# Manually specify a fuchsia checkout to run on
shush lint_file.json --lint clippy::style --fuchsia-dir ~/myfuchsia fix

# Run shush on itself
fx clippy '//tools/shush(//build/toolchain:host_x64)' --raw |
    shush --force --lint clippy::needless_borrow fix

# Emit markdown (useful for creating bugs)
shush lint_file.json --lint clippy::absurd_extreme_comparisons allow --markdown
```

Run `fx shush --help` for details.
