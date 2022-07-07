# Shush

This tool consumes json diagnostics emitted by the rust compiler (and clippy), and tries to address them by either inserting `#[allow(...)]` annotations or applying compiler-suggested fixes to the code automatically. It can be used to make large scale changes to roll out new clippy lints, perform edition migration, and address new compiler warnings from upstream.

# Usage:

``` sh
# Allow a specific lint or category
fx clippy -f <source file> --raw | shush --lint clippy::suspicious_splitn --mock allow

# See all clippy lints in our tree
fx clippy --all --raw | shush --lint clippy::all --dryrun --mock allow

# Manually specify a fuchsia checkout to run on
shush lint_file.json --lint clippy::style --fuchsia-dir ~/myfuchsia fix

# Run shush on itself
fx clippy '//tools/shush(//build/toolchain:host_x64)' --raw |
    shush --force --lint clippy::needless_borrow fix

# Emit markdown (useful for creating bugs)
shush lint_file.json --lint clippy::absurd_extreme_comparisons allow
```

Run `fx shush --help` for details.

## Performing a lint rollout

`shush` can perform large-scale lint rollouts across the tree automatically:

1. Create a tracking issue by hand. This can be done from the Monorail UI. Keep the issue number for later.
2. Get a copy of `prpc`. You can find packages under `infra/tools/prpc` on the [CIPD site](https://chrome-infra-packages.appspot.com/p/infra/tools/prpc) or through the command line `cipd` client.
3. Pick a commit to generate codesearch links with. This should be a commit prior to the lint rollout CL.
4. Write an issue description template in Markdown. The file should contain `INSERT_DETAILS_HERE` where code links should be added.
5. `fx clippy --all --raw | shush --lint $LINT --prpc $PRPC_PATH allow --codesearch_tag $COMMIT --template $TEMPLATE --blocking-issue $ISSUE`
6. Land a CL with the generated changes.
7. `shush --prpc $PRPC_PATH rollout`

`shush allow` also takes additional arguments for customizing the generated issues, including CC limits and issue labels.
