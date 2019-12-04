# Test data for zxdb

This is used for the symbol parsing tests.

There are two flavors of tests. Some build the symbol test and run on that, expecting to find
certain symbol patterns.

Tests that rely heaving on the binary layout use the checked-in version to avoid compiler and
platform variations.

## To generate the binaries

  * On x64 copy the generated `zxdb_symbol_test` that includes symbols to
    `libsymbol_test_so.targetso`. As of this writing, the compiled file will be something
    like: `out/x64/host_x64/test_data/zxdb/libzxdb_symbol_test.targetso`

  * The Linux "strip" doesn't work on our binaries. Copy the stripped version from
    `out/x64/x64-shared/libzxdb_symbol_test.so` to `libsymbol_test_so_stripped.targetso`

# Large tests

There are some additional binaries that are too large to include with every checkout. These are
used by the `zxdb_large_tests` target and appear in `//prebuilts/test_data/debug/large_test_data`.

To make these work you need to download them and enable the target that requires them.

### Downloading the large test data

This package is Google-internal-only so we have the option to add different binaries regardless of
licences or whether they're unreleased. To opt-in to downloading these binaries:

```
jiri init -fetch-optional=zxdb-large-test-data
jiri update
```

If that doesn't work, it's possible you need the internal version of the integration repository
checked out. See the Google-internal checkout instructions for how to do this.

### Enabling the zxdb large tests

There is a GN build flag `include_zxdb_large_tests` which defaults to `false`. This controls whether
the target `out/<arch>/host_x64/zxdb_large_tests` are compiled. To set this to true, run this,
substituting `<arch>` with your build directory:

```
fx gn args out/<arch>
```

and add a line to the bottom of the file:

```
include_zxdb_large_tests = true
```

## Adding to the large test data

The file `large_test_data.yaml` in this directory is the CIPD specification for the package. To add
files, add them to `//prebuilt/test_data/debug/large_test_data` alongside the existing ones. Add
the file names to the .yaml file. _Be sure to add licenses and READMEs for each file added,
including whether it needs to be internal-only or not._

Each package is identified by a version tag. This tag will be used later to specify which package
to fetch. We use the tag name `git_revision:...` where "..." is the git hash of the current
tip-of-tree. This way it's always unique (otherwise the tags are basically free-form).

Copy `large_test_data.yaml` from this directory to `//prebuilt/test_data/debug/large_test_data`
since CIPD wants everything to be in the same directory while it's working. Be sure to check in the
final version to this directory so it's always current.

The atomic way to make a package and upload with a version tag is:

```
cipd pkg-build -pkg-def large_test_data.yaml -out large_test_data.cipd
cipd pkg-register -tag git_revision:fa316074ae0f53a2562c76cb4637b6f2892b02f7 large_test_data.cipd
```

The other way to do this is make and upload a package and set the version tag later. To build and
upload:

```
cipd create -pkg-def large_test_data.yaml
```

To list the instances of this package once uploaded:

```
cipd instances fuchsia_internal/test_data/debug/large_test_data
```

To see the version tag, specify the "instance ID" from the above list:

```
cipd describe fuchsia_internal/test_data/debug/large_test_data -version FZIh6hPkZOZa7vbUXjLjUnfzu3tYxnZ-NRHLyf2HnrMC
```

To explicitly set a version tag for an instance:

```
cipd set-tag fuchsia_internal/test_data/debug/large_test_data -version FZIh6hPkZOZa7vbUXjLjUnfzu3tYxnZ-NRHLyf2HnrMC -tag git_revision:fa316074ae0f53a2562c76cb4637b6f2892b02f7
```

## Fetch configuration

To cause the package to be downloaded, there is an entry in the
[prebuilts](https://fuchsia.googlesource.com/integration/+/refs/heads/master/prebuilts) file of
`//integration` (in the internal repo it's in the `fuchsia` subdirectory).

To update to a new package, you must have the internal integration repository checked out. See the
internal checkout instructions for how to do this.

Then substitute the tag you specified in in the CIPD package in the "version" field of the prebuilts
file.

Don't forget to run `//integration/update-lockfiles.sh` after making changes to `prebuilts`!

Note that the "attributes" attribute specified that this package won't be downloaded unless the user
opts-in as described earlier.

To test that the updated download works:

```
jiri fetch-packages -local-manifest=true
```
