# cmx2cml

An experimental tool to make it easier to migrate fuchsia.git v1 components to
v2.

This is an aid to save time during go/cmxtinction, and users should expect to
perform manual work to make the generated files acceptable. It is still the
user's responsibility to:

* select the correct runner for the new manifest
* integrate the new file with the build
* declare any `expose`d capabilities
* add inspect shards if applicable
* (for system components) add the component to the v2 topology
* route any required capabilities in the surrounding topology
* update the storage index and/or diagnostics selectors

See comments in the generated CML for more detailed follow-up actions.

## Usage

Run a build and then `fx cmx2cml --runner RUNNER PATH_TO_CMX`. Runner options:

* `elf`
* `elf-test`
* `rust-test`

If conversion is successful a CML file will be written alongside the original
CMX. You can override the output location with `--output path/to.cml`.

## Getting Help

See the overall docs for the [Components v2 migration].

As with all aspects of the components migration, reach out to the Component
Framework team if you have issues.

## Known limitations of produced CML

v1 manifests do not require components to list which protocols they expose to
their parent, which means that this tool cannot statically know what to put in
a CML file's `expose` section.

The only test runner supported is the ELF test runner, because it allows
transparently migrating args and env vars. Users can follow up and choose a more
featureful test runner for their language as a follow-up.

The converter only reasons about children when migrating `injected-services`
declarations, otherwise it is assumed that any existing v1 children will stay
as v1 components with their lifecycle managed dynamically using `fuchsia.sys.*`.

Children added to replace injected services may need additional capabilities
routed to them.

## Known CMX feature gaps

> Note: while there are significant limitations to this approach, this tool was
able to convert ~620 of ~900 CMX files in fuchsia.git at time of writing without
encountering any of the below gaps.

The initial version of this tool does not support converting Dart components.

This tool does not expand shards before converting, so CMX files which rely on
shards to populate key portions of e.g. `program` will not work.

Not all providers of `injected-services` have known v2 equivalents to use.

Tests which specify args for the components listed in `injected-services` are
not supported, as v2 static children must be referenced solely by URL.

Components which request `sandbox.boot`, `sandbox.pkgfs`, or `sandbox.system`
are not supported.

Components which use the `hub` feature will need to manually add the appropriate
uses for events and migrate their tests' code to make use of the new protocols.

Components which use the `deprecated-ambient-replace-as-executable`,
`deprecated-shell` or `deprecated-global-dev` features are not supported.

Components which request device directories other than those offered by
test_manager are not supported.

## Testing

See `tests/goldens` for some example CMX files that are converted to CML by
the build.

When making changes that introduce new errors or panics it can be useful to run
this tool on ~all CMX files available. Try running `./try_convert_all_cmx.sh` to
see the tool in action. Beware it will leave several hundred possibly-valid CML
files in your tree. If that script prints errors that are due to known
limitations, add the CMX file to the list of exceptions at the top of the
script. Consider adding new CMX->CML goldens if your change changes behavior
that is only executed by this script.

[Components v2 migration]: https://fuchsia.dev/fuchsia-src/development/components/v2/migration?hl=en
