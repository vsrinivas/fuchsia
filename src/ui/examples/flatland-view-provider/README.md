# `flatland-view-provider` example app

There are three components defined here (plus tests):
- `flatland-view-provider.cm`
- `flatland-view-provider-vulkan.cm`
- `flatland-view-provider-example.cmx`
  - (deprecated; used only for integration tests)

`flatland-view-provider.cm` is a simple example app which uses Flatland to display
content.  It displays colored squares which rotate colors through time.  It also listens
to touch events and echoes them to the syslog.

`flatland-view-provider-vulkan.cm` is the same, except it uses Vulkan to render, instead
of negotiating CPU-writable buffers with sysmem.

TODO(fxbug.dev/104692): the Vulkan version uses a single filled rect, instead of rendering
each quadrant in a different color.

To launch:
`ffx session add fuchsia-pkg://fuchsia.com/flatland-examples#meta/flatland-view-provider.cm`
or
`ffx session add fuchsia-pkg://fuchsia.com/flatland-examples#meta/flatland-view-provider.cm`
