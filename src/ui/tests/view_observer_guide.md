# View Observer Guide

This doc aims to guide developers on how to use
[`fuchsia.ui.observation.geometry.ViewTreeWatcher`](/sdk/fidl/fuchsia.ui.observation.geometry/watcher.fidl)
in their tests.

## Overview

Each view in a view tree will have its own size, position, orientation, and
coordinate system. Collectively, these are the "geometric" properties of views.
Along with the geometric properties, each view also has a set of "topological"
properties namely information about its children and its parent. The
`fuchsia.ui.observation.geometry.ViewTreeWatcher` provides geometrical and
topological updates to a client for a view tree over which it has authority.
Currently, this protocol is only available for tests.

## How the API works

A view tree contains the hierarchy of the views in a scene graph along with hit
testing and per-view data such as transforms and bounding boxes. A view tree
snapshot captures this information every frame after scenic applies all the
updates to the scene graph. This view tree snapshot is then used by the server
for extracting the geometric data of the view. To receive geometry updates,
clients connect to `fuchsia.ui.observation.geometry.ViewTreeWatcher`. The server
for this protocol subscribes to the newly generated view tree snapshots. When a
new snapshot gets generated, the server appends it to the buffer of every
client.

Consider the following example:-

![some text](//src/ui/tests/images/view_observer_guide.png)

The snapshot generated from the first frame has just the one view. Response of a
`fuchsia.ui.observation.geometry.ViewTreeWatcher.Watch` call will be as
follows:-
* `WatchResponse.updates.size` : 1 (One snapshot generated for the frame)
* `WatchResponse.updates[0].views.size` : 1 (There is only 1 view in the scene)
* `ViewDescriptor.layout.extent.min` : (0,0)
* `ViewDescriptor.layout.extent.max` : (5,5)
* `ViewDescriptor.extent_in_context.origin` : (0,0)
* `ViewDescriptor.extent_in_context.width` : 5
* `ViewDescriptor.extent_in_context.height` : 5
* `ViewDescriptor.extent_in_context.angle_degrees` : 0

`ViewDescriptor.extent_in_parent` will be the same as
`ViewDescriptor.extent_in_context` as the parent view is the only view in the
scene.

The snapshot generated from the second frame has child view attached to the
parent view. Response of a
`fuchsia.ui.observation.geometry.ViewTreeWatcher.Watch` call will be as
follows:-

*   `WatchResponse.updates.size` : 1 (One snapshot generated for the frame)
*   `WatchResponse.updates[0].views.size` : 2 (There are 2 views in the scene)

The `ViewDescriptor` for the parent view will be the same as above. For the
child view, the `ViewDescriptor` will look like this:

*   `ViewDescriptor.layout.extent.min` : (0,0)
*   `ViewDescriptor.layout.extent.max` : (2,3)
*   `ViewDescriptor.extent_in_context.origin` : (1,1)
*   `ViewDescriptor.extent_in_context.width` : 3
*   `ViewDescriptor.extent_in_context.height` : 2
*   `ViewDescriptor.extent_in_context.angle_degrees` : 90

`ViewDescriptor.extent_in_parent` will be the same as
`ViewDescriptor.extent_in_context` as the parent view and the root view are the
same in this case.

Clients should not write tests in a manner which associates the number of
snapshots returned in the response to the number of frames. It is possible for
multiple snapshots to be generated for a frame and depends on the working of the
frame scheduler. Note that it is illegal for the client to make another
`fuchsia.ui.observation.geometry.ViewTreeWatcher.Watch` call when one is still
in-flight. If this happens, the server closes the channel and removes the client
from its registry.

Clients using `fuchsia.ui.observation.test.Registry` should not rely on the
number of views present in the response of a
`fuchsia.ui.observation.geometry.ViewTreeWatcher.Watch` call to check if their
view is connected. Instead, they should watch for their view to be present in
the response. In order to do that, the client should check for the presence of
their view's viewRefKoid in the response.

## Using the View Observer API

Follow these steps to use the Observer API in your tests.

1.  Register the `fuchsia.ui.observation.geometry.ViewTreeWatcher` endpoint with
    `fuchsia.ui.observation.test.Registry.RegisterGlobalViewTreeWatcher`.
    This allows the client to get the global view of the view tree. The client
    can pass a custom callback to get notified when the endpoint has been
    registered with `GeometryProvider`.

    Example:
    ```
    fuchsia::ui::observation::test::RegistryPtr observer_registry_ptr_;
    fuchsia::ui::observation::geometry::ViewTreeWatcherPtr geometry_provider;
    std::optional<bool> result;

    // Register the endpoint with the GeometryProviderManager.
    observer_registry_ptr_->RegisterGlobalViewTreeWatcher(geometry_provider.NewRequest(),
    [&result] { result = true; });

    // Wait for the endpoint to be registered.
    RunLoopUntil([&result] { return result.has_value(); });
    ```

2.  Use the same endpoint to call
    `fuchsia.ui.observation.geometry.ViewTreeWatcher.Watch` to receive updates
    from the time when the endpoint was registered with the
    `GeometryProvider`.

    Example:
    ```
    std::optional<fuog_WatchResponse> geometry_result;

    geometry_provider->Watch(
        [&geometry_result](auto response) { geometry_result = std::move(response); });

    RunLoopUntil([&geometry_result] { return geometry_result.has_value(); });
    ```

3.  In order to check whether a view is present in the view tree, check whether
    the view exists in the `updates` vector in
    `fuchsia.ui.observation.ViewTreeWatcher.WatchResponse`. If the view is present
    in the response, it is guaranteed to be present in the view tree. The presence of a
    view in the view tree is necessary but not sufficient to receive input/focus events.

    Example:
    ```
    std::optional<fuog_WatchResponse> geometry_result;

    geometry_provider->Watch( [&geometry_result](auto response) {
    geometry_result = std::move(response); });

    RunLoopUntil([&geometry_result] { return geometry_result.has_value(); });

    auto& updates = geometry_result->updates();

    // Wait till the view with |view_ref_koid| is present in the response
    (connected in the view tree).
    RunLoopUntil([&updates, &view_ref_koid]{
        for (const auto& snapshot : updates){
            for(const auto& views : snapshot.views()){
                if(view.view_ref_koid() == view_ref_koid){
                    return true;
                }
            }
        }
        return false;
    });
    ```

    Before waiting for a view to be present in the response, make sure the tests
    exercises some operations on the view so that the view gets created.

## Advantages

`fuchsia.ui.observation.geometry.ViewTreeWatcher` provides strong
synchronization semantics. If a view is present in
`fuchsia.ui.observation.geometry.WatchResponse`, then the test
can correctly inject input or move focus to that view without a race condition,
provided that the view is ready to receive input and focus events.

## Interpreting the result.

1.  `fuchsia.ui.observation.geometry.WatchResponse` contains the
    views and the geometrical information about every view which is present in
    the view tree. Refer to this
    [FIDL](/sdk/fidl/fuchsia.ui.observation.geometry/watcher.fidl) to get the
    details on what all information is returned in the response.
2.  It is possible that some snapshots might get dropped in the response. In
    that case, `error` in a
    `fuchsia.ui.observation.geometry.WatchResponse` will be
    present and will contain the reasons for dropping the snapshots.

## References

1.  [fuchsia.ui.observation.geometry.ViewTreeWatcher](/sdk/fidl/fuchsia.ui.observation.geometry/watcher.fidl) protocol
