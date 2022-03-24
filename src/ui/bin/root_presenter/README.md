# Root Presenter

This component is being deprecated, with:
- [Input Pipeline](./../input-pipeline/README.md) managing input device lifecycle and lower-level
  input event dispatch, and
- [Scene Manager](./../scene_manager/README.md)) creating the root of the global scene graph and
  connecting root-level Views by clients such as Sys UI.

Once the above features in root presenter are replaced, this component still provides virtual
keyboard functionality.

Please reach out to the OWNERS to coordinate any intended work related to this component and its
current or future responsibilities.

## Usage

This program is a server, and so is not started directly. See the `present_view` tool.

## CMX file and integration tests

Note that the `meta/` directory has two CMX files. One is for production, the
other for tests.

The production package `//src/ui/bin/root_presenter:root_presenter` includes
`meta/root_presenter.cmx`, which exists to enable access to the input device files in
`/dev/class/input-report`. The regular content is pulled in from
`meta/root_presenter_base.cmx`.

Test packages should include `//src/ui/bin/root_presenter:component_v1_for_test`
and launch it with `fuchsia-pkg://fuchsia.com/<your-test-package>#meta/root_presenter.cmx`.
This test-only Root Presenter omits the driver access. Generally, test packages
should include their own copy of a component to ensure hermeticity with respect
to package loading semantics.

Integration tests don't require access to the device files, because (1) input
injection occurs at a different protocol in Root Presenter, and (2) exposure to
the actual device files is a flake liability for these tests.

During regular maintenance, when adding a new service dependency, add it to
`meta/root_presenter_base.cmx`, so that it is seen in both tests and production.
