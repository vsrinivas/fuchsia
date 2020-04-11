# How to enable verbose logging

At times, it is handy to have extra logging for input event dispatch, so that we
can see what (and how) components are handling event dispatch. We can enable
verbose logging for a particular component by adding the `--verbose` flag to its
invocation.

For example, `Root Presenter` and `Scenic` components can issue verbose logging.

If a test has a CMX file that starts a `Scenic` component in its
`injected-services` clause, that looks like the following:

```json
{
    "facets": {
        "fuchsia.test": {
            "injected-services": {
                "fuchsia.ui.scenic.Scenic": "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx",
```

To add extra logging for input event dispatch, modify the line that contains
"fuchsia.ui.scenic.Scenic", in the following way:

```json
                  "fuchsia.ui.scenic.Scenic": [ "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx", "--verbose=2" ],
```

Note that each service instance of `Scenic` (or `Root Presenter`) must be
modified, unless you know which service is invoked first.

This is identical to modifying a `sysmgr`
[services configuration file](/src/sys/sysmgr/sysmgr-configuration.md).
