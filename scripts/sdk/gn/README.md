# GN SDK

The GN SDK frontend produces a [GN](https://gn.googlesource.com/gn/+/refs/heads/master/README.md) workspace.

## Directory structure

- `generate.py`: the script that generates the SDK;
- `templates`: Mako templates used to produce various SDK files;
- `base`: SDK contents that are copied verbatim;
- `testdata`: files used as input during tests
- `golden`: files used to verify generator output during tests

## Generating

In order to generate the GN SDK, point the `generate.py` script to an
SDK archive, e.g.:

```sh
$ scripts/sdk/gn/generate.py \
    --archive my_sdk_archive.tar.gz \
    --output gn_sdk_dir/
```

### Getting the latest Core SDK:

```sh
$ BUILD_ID="$(gsutil cat gs://fuchsia/development/LATEST_LINUX)" &&
    gsutil cp gs://fuchsia/development/$BUILD_ID/sdk/linux-amd64/core.tar.gz .
```

### Testing

#### SDK generator tests

To test the generator, run the `test_generate.py` script.

```sh
$ scripts/sdk/gn/test_generate.py
```

This runs the generator against the `testdata` directory and compares the output
to the files in the `golden` directory.

After making changes to the generator, update the contents of `testdata` as
needed to exercise your new code, then run the `update_golden.py` script to fix
the `golden` files.

```sh
$ scripts/sdk/gn/update_golden.py
```

Commit your changes to the generator, `testdata` contents, and `golden` contents
together.

#### Bash scripts test

To test the bash scripts, run the `bash_tests/run_bash_tests.sh` script.

```sh
$ scripts/sdk/gn/bash_tests/run_bash_tests.sh
```

This runs the tests in the `scripts/sdk/gn/bash_tests` which tests the scripts found in `scripts/sdk/gn/base/bin`.

#### Test project test

To test the SDK on a test project, the `generate.py` must be run with the `--tests` flag. `generate.py` requires a IDK (integrator development kit) build

1. Create a temporary directory with subdirectories for the SDK and test
   workspaces:

   ```sh
   mkdir -p temp/gn_sdk_dir temp/test_workspace
   ```

1. Download the latest IDK (integrator development kit) to a temporary
directory:

   ```sh
   $ BUILD_ID="$(gsutil cat gs://fuchsia/development/LATEST_LINUX)"
   gsutil cp gs://fuchsia/development/$BUILD_ID/sdk/linux-amd64/core.tar.gz temp/idk.tar.gz
   ```
1. Generate the test workspace into a temporary directory:

   ```sh
   $ scripts/sdk/gn/generate.py \
    --archive temp/idk.tar.gz \
    --output temp/gn_sdk_dir/ \
    --tests temp/test_workspace
   ```

1. Run the `run.py` file in the test workspace:

   ```sh
   $ temp/test_workspace/run.py
   ```
