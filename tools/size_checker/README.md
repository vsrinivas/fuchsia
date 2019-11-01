# Size Checker
A executable that checks if a build exceeds its allocated space limit.


## Local Run
Run the following command in the output directory:

```bash
fx size_checker --build-dir .
```

## Space Limit
The space limit is governed by the `size_checker.json`, which is generated based on the `size_checker_input` argument.

This argument is typically defined in the `$product.gni` file of the current `product.board` configuration.

For a full description of its argument, see `cmd/BUILD.gn`.

## Dependencies

The test has many hardcoded dependencies, so you only need to pass in the build directory.

It expects to find the following files in the output directory:

```
blob.sizes
gen/build/images/blob.manifest.list
size_checker.json
```

Additionally, it also depends on the `blobs.json` of each package to be in the same directory as the `meta.far` file.
