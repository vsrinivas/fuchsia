# Size Checker
A executable that checks if a build exceeds its allocated space limit.


## Local Run
Run the following command in the output directory:

```bash
fx size_checker --build-dir .
```

## Space Limit
The space limit is governed by the `size_checker.json`, which is generated based on the `size_checker_input` argument.

To enforce the size limit, build product with the suffix `_size_limits` (if
such file exists) instead of the normal product.gni.

For a full description of its argument, see `cmd/BUILD.gn`.

## Dependencies

The test has many hardcoded dependencies, so you only need to pass in the build directory.

It expects to find the following files in the output directory:

```
blobs.json
gen/build/images/blob.manifest.list
size_checker.json
```

Additionally, it also depends on the `blobs.json` of each package to be in the same directory as the `meta.far` file.
