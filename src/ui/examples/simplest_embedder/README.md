# Simplest Embedder

This is the usage example for the
[`scenic::BaseView`](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/lib/ui/base_view/base_view.h)
class, which simplifies the creation of components that can act as both parents
and children in the Scenic view tree.

## Note on `ExamplePresenter`

Includes a simple implementation of
[`fuchsia.ui.policy.Presenter`](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.ui.policy/presenter.fidl)
which it uses instead of connecting to `root_presenter`; the topology is
analogous to how Peridot's `basemgr` connects the `base_shell` to
`root_presenter`.

This configuration requires connecting directly to Scenic; it will not work if
there is already a `Compositor` attached to the default display.

## Note on Input Events

For Scenic to deliver input events to Views, it must first receive them from a
presenter. It is the presenter's responsibility to set up reception of input
events from Zircon and pass them to Scenic. Today, only the `root_presenter`'s
implementation is currently configured to do that; `ExamplePresenter` could do
it with extra work.

Both applications (`simplest_embedder` and `shadertoy_client`) will respond to
touchscreen events.

## Usage:

*   Connect to the root presenter:
    *   `$ run fuchsia-pkg://fuchsia.com/simplest_embedder#meta/simplest_embedder.cmx --use_root_presenter`
*   Set up an example presenter:
    *   `$ run fuchsia-pkg://fuchsia.com/simplest_embedder#meta/simplest_embedder.cmx --use_example_presenter`
    *   For `ExamplePresenter`, make sure to kill any instances of `scenic` and
        `root_presenter` (use `killall`).
*   Stand up the view provider service, and have it connect to an ambient
    `root_presenter`. In this configuration, the `simplest_embedder` application
    won't put up its own View.
    *   `$ present_view fuchsia-pkg://fuchsia.com/simplest_embedder#meta/simplest_embedder.cmx`
