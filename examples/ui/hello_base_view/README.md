# Hello BaseView

This is the usage example for the
[`scenic::BaseView`](https://fuchsia.googlesource.com/garnet/+/master/public/lib/ui/base_view/cpp/base_view.h)
class, which simplifies the creation of components that can act as both parents
and children in the Scenic view tree.

## Note on `ExamplePresenter`

Includes a simple implementation of
[`fuchsia.ui.policy.Presenter2`](https://fuchsia.googlesource.com/garnet/+/master/public/fidl/fuchsia.ui.policy/presenter.fidl)
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

Both applications (`hello_base_view` and `shadertoy_client`) will respond to
touchscreen events.

## Usage:

*   Connect to the root presenter:
    *   `$ run hello_base_view --use_root_presenter`
*   Set up an example presenter:
    *   `$ run hello_base_view --use_example_presenter`
    *   For `ExamplePresenter`, make sure to kill any instances of `scenic` and
        `root_presenter` (use `killall`).
*   Stand up the view provider service, and have it connect to an ambient
    `root_presenter`. In this configuration, the `hello_base_view` application
    won't put up its own View.
    *   `$ present_view hello_base_view`
