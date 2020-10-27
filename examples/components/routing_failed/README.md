# Failed routing Example

This directory contains an example of failed [capability
routing][capability-routing] in [Component Framework][cf-intro] ([Components
v2][cfv2]).

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples
$ fx build
```

(Disclaimer: if these build rules become out-of-date, please check the
[Build documentation](docs/development/workflows) and update this README!)

## Running

Provide the `echo_realm` component's URL to `run` as an argument to `component_manager`:

```bash
$ fx shell 'run fuchsia-pkg://fuchsia.com/component_manager#meta/component_manager.cmx fuchsia-pkg://fuchsia.com/components-routing-failed-example#meta/echo_realm.cm'
```

This will run the component in an instance of component manager as a v1
component.

Make sure you have `fx serve` running in another terminal so your component can
be installed!

To see component manager's log of the failed capability routing, run:

```bash
$ fx klog
```

[capability-routing]: /docs/concepts/components/v2/component_manifests.md#capability-routing
[cf-intro]: /docs/concepts/components/v2/introduction.md
[cfv2]: /docs/glossary.md#components-v2
