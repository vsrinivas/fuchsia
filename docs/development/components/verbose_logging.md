# Enable verbose logging for input events

Adding extra logging for input event dispatch allows you to see what (and how)
components are handling event dispatch.

To enable vebose logging for components that use
<code>[fxl](/docs/development/languages/c-cpp/logging.md)</code> to parse their
args, add the `--verbose` flag to its invocation. For example, `Root Presenter`
and `Scenic` components can issue verbose logging.

The following sample code shows the
[component manifest](/docs/concepts/components/v1/component_manifests.md) (`.cmx`) of a
test that starts a `Scenic` component in its `injected-services` clause:

```json
{
  "facets": {
    "fuchsia.test": {
      "injected-services": {
        "fuchsia.ui.scenic.Scenic": "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx",
```

To add extra logging for input event dispatch, modify the
`fuchsia.ui.scenic.Scenic` line in the following way:

```json
"fuchsia.ui.scenic.Scenic": [ "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx", "--verbose=2" ],
```

Each service instance of `Scenic` (or `Root Presenter`) must be
modified, unless you know which service is invoked first.

In most ways, this is identical to modifying a
<code>[sysmgr](/src/sys/sysmgr/sysmgr-configuration.md)</code> services
configuration file.
