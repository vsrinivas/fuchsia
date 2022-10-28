# Rust Fuchsia 2021 Edition Migration

1.  Update `rustc_artifact.gni` to support new edition.
2.  Edit [clippy.gni] to add `--force-warn=rust-2021-compatibility` to args.
3.  Run `fx clippy --all --raw > lints.json` (make sure to add repos `--with
    //bundles/kitchen_sink --with //bundles/buildbot/core`)
4.  Run `fx shush lints.json --lint rust-2021-compatibility fix` (after building
    shush `fx build host-tools/shush`)
    *   Get the CL approved via global OWNERS?
    *   Add a description of the changes to the CL description
5.  Figure out what targets it ran on
    *   `jq '.[] | select(.src[0] | startswith("..")) | .original'
        out/edition_migration/clippy_target_mapping.json -r | sed 's/(.*$//' |
        sort -u > /tmp/clippy_targets.txt`
6.  change `edition = 2021` for each of them in BUILD.gn.
    *   https://fuchsia.googlesource.com/infra/infra/+/refs/heads/main/codifier/
    *   `go run cmd/examples/rust_migration "$FUCHSIA_DIR"
        /tmp/clippy_targets.txt | tee /tmp/migration_log.txt`
7.  Find anything which doesn't set edition, and set `edition = 2018` and make
    `edition` required
8.  Maybe later change `edition` to default to 2021 instead of being required.
    Or not.

## Other Stuff

* The rustfmt.toml in our root directory needs to be updated to 2021 edition
  (this didn't cause any formatting changes)
* Grep for BUILD file snippets in docs to update any examples to the new edition

[clippy.gni]: https://cs.opensource.google/fuchsia/fuchsia/+/main:build/rust/clippy.gni

## Live 2021 edition migration log

```
jiri update -gc -v

# add `--force-warn=rust-2021-compatibility`
vim ./build/rust/clippy.gni

git commit -a -m "throwaway"

fx set core.x64 --with //bundles:kitchen_sink --with //bundles/buildbot:core

fx clippy --all --raw > lints.json

fx build host-tools/shush
fx shush lints.json --lint rust-2021-compatibility fix

# Pending https://fxbug.dev/101439 fix
git restore src/settings/service

git commit -a -m "[rust] Edition 2021 fixes"
jq '.[] | select(.src[0] | startswith("..")) | .original' out/default/clippy_target_mapping.json -r | sed 's/(.*$//' | sort -u > /tmp/clippy_targets.txt

cd ~/infra/fuchsia/infra/codifier
jiri update -gc -v
go run cmd/examples/rust_migration/main.go "$FUCHSIA_DIR" /tmp/clippy_targets.txt | tee /tmp/migration_log.txt

git restore src/settings/service
```

At this point, manual fixups are required:

```
$ grep "couldn't find" /tmp/migration_log.txt| grep -v //src/tests/fidl/source_compatibility/
    couldn't find //src/developer/ffx/daemon/protocols:ffx_daemon_protocols_lib
    couldn't find //src/developer/ffx:ffx
    couldn't find //src/lib/fuchsia-cxx/examples/basic:example_blobstore_rustc_library
    couldn't find //src/storage/fuchsia-fatfs:fatfs
    couldn't find //src/storage/fxfs:fxfs
    couldn't find //src/sys/pkg/lib/fuchsia-pkg:lib_test
```

Manually add `edition = "2021"` to each of the targets in ^

`rust_cxx_ffi_library("example_blobstore")` and
`ffx_protocols("ffx_daemon_protocols")` hardcodes edition 2018 (and doesnt
forward from invocations) so we left it alone

`fx gen` fails due to component_manager (another template which sets edition
itself). This time we updated the template definition to the new edition.

`fx gen` succeeds

2015 edition opt-outs: * src/lib/mundane/BUILD.gn `git restore --source HEAD^
src/lib/mundane/` * `git restore --source HEAD^
third_party/rust_crates/compat/rustyline` * `git -C
third_party/rust_crates/mirrors/rustyline reset --hard` * `git -C
third_party/rust_crates/mirrors/quiche reset --hard` * `git -C
third_party/alacritty reset --hard`

`fx build` (fails due to TryInto)

`git fetch https://fuchsia.googlesource.com/fuchsia refs/changes/14/684214/5 &&
git cherry-pick FETCH_HEAD`

`fx build` (works this time)
