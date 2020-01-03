# Bouncing Ball Example

This directory contains an example application which draws an animated bouncing
ball using Scenic. This README explains how it's built.

## Running the example

From garnet, this can be run with:
```
present_view fuchsia-pkg://fuchsia.com/bouncing_ball#meta/bouncing_ball.cmx
```

From topaz, this can be run with:

```
sessionctl add_mod fuchsia-pkg://fuchsia.com/bouncing_ball#meta/bouncing_ball.cmx
```

In garnet, `Alt`+`Esc` toggles back and forth between the console and graphics.

## Getting Started: Exposing a ViewProvider

For an application to draw any UI, it first needs to implement a service to
expose that UI externally. That protocol is called
[`ViewProvider`](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.ui.app/view_provider.fidl).

First, let's publish our `ViewProvider` protocol to our outgoing services. We don't
do anything with incoming request for a `ViewProvider` yet.

``` cpp
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::unique_ptr<sys::ComponentContext> component_context =
      sys::ComponentContext::Create();

  // Add our ViewProvider service to the outgoing services.
  component_context->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
      [](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
        // TODO: Next step is to handle this request.
      });

  loop.Run();
  return 0;
}
```

## Implementing ViewProvider

Next, let's implement `ViewProvider`. `ViewProvider` implements one method, `CreateView`.
`CreateView` is called by clients of our `ViewProvider` when they want our process to create UI.
Most often, this is when a parent process (such as a Session Shell) wants to
show a child process's UI.

This implementation of `ViewProvider` doesn't do anything in `CreateView` quite yet.
``` cpp
class ViewProviderService : public fuchsia::ui::app::ViewProvider {
 public:
  ViewProviderService(sys::ComponentContext* component_context)
      : component_context_(component_context) {}

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services)
      override {
    // TODO: Use the |view_token| and Scenic to attach our UI.
  }

  void HandleViewProviderRequest(
      fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  sys::ComponentContext* component_context_ = nullptr;
  std::vector<std::unique_ptr<BouncingBallView>> views_;
  fidl::BindingSet<ViewProvider> bindings_;
};
```

Next, let's wire up this class in `main()`:

``` cpp
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::unique_ptr<sys::ComponentContext> component_context =
      sys::ComponentContext::Create();

  // ** NEW CODE **: Instantiate our ViewProvider service.
  ViewProviderService view_provider(component_context.get());

  // Add our ViewProvider service to the outgoing services.
  component_context->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
       // ** NEW CODE **: Handle ViewProvider request.
       [&view_provider](
          fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
        view_provider.HandleViewProviderRequest(std::move(request));
      });

  loop.Run();
  return 0;
}
```

## Attaching a UI

On Fuchsia, a component creates a UI by communicating to the graphics compositor, Scenic, over a
FIDL protocol.
More specifically, the component uses a `Session` protocol, which allows a client to enqueue a
list of `Commands` that can create and modify objects called `Resources`. `Resources` describes a
variety of types which includes nodes, materials, textures, and shapes, among others.

A client creates a scene graph by creating and modifying the `Resources`.

The _root_ of a client's scene graph is called a `View`. A `View` also has an associated size,
which we will talk about later. First, however, let's create a class `BouncingBallView` which
creates a `Session` and uses that to create a `View`.

``` cpp
class BouncingBallView : public fuchsia::ui::scenic::SessionListener {
 public:
  BouncingBallView(sys::ComponentContext* component_context,
                           zx::eventpair view_token)
      : session_listener_binding_(this) {
    // Connect to Scenic.
    fuchsia::ui::scenic::ScenicPtr scenic =
        component_context
            ->svc()->Connect<fuchsia::ui::scenic::Scenic>();

    // Create a Scenic Session and a Scenic SessionListener.
    scenic->CreateSession(session_.NewRequest(),
                          session_listener_binding_.NewBinding());

    InitializeScene(std::move(view_token));
  }
```

In `InitializeScene`, we create a list of `Commands`, then `Enqueue` them.
However, the commands are not applied until `Present` is called. `Present`
takes a presentation time when the commands should be applied; it is
acceptable to call it with a time of `0` but subsequent calls to `Present` should use
presentation times based on the `PresentationInfo` we receive from Scenic.

``` cpp
  static void PushCommand(std::vector<fuchsia::ui::scenic::Command>* cmds,
                          fuchsia::ui::gfx::Command cmd) {
    // Wrap the gfx::Command in a scenic::Command, then push it.
    cmds->push_back(scenic::NewCommand(std::move(cmd)));
  }

  void InitializeScene(zx::eventpair view_token) {
    // Build up a list of commands we will send over our Scenic Session.
    std::vector<fuchsia::ui::scenic::Command> cmds;

    // View: Use |view_token| to create a View in the Session.
    PushCommand(&cmds, scenic::NewCreateViewCmd(kViewId, std::move(view_token),
                                                "bouncing_ball_view"));

    session_->Enqueue(std::move(cmds));

    // Apply all the commands we've enqueued by calling Present. For this first
    // frame we call Present with a presentation_time = 0 which means it the
    // commands should be applied immediately. For future frames, we'll use the
    // timing information we receive to have precise presentation times.
    session_->Present(0, {}, {},
                      [this](fuchsia::images::PresentationInfo info) {
                        // This is the callback after our changes are presented.
                        // TODO: Render a new frame
                      });
}
```

## Creating a UI

Once we've created a `View` resource, we can now attach our UI to it.

We attach a `EntityNode` to it, and to that, we attach `ShapeNodes`. We also create `Materials`
for the `ShapeNodes`.

Note, however, that this code is not sufficient to draw a UI. We also need to attach a `Shape`
(which represents the geometry to render) to a `ShapeNode` for it to render. However, we don't
yet know the size of our UI and therefore we postpone creating `Shapes` until we do, in the
next section.

Here, we create a node for a pink background and for a purple circle we will
draw on top of it.

``` cpp
void InitializeScene(zx::eventpair view_token) {
  // Build up a list of commands we will send over our Scenic Session.
  std::vector<fuchsia::ui::scenic::Command> cmds;

  // View: Use |view_token| to create a View in the Session.
  PushCommand(&cmds, scenic::NewCreateViewCmd(kViewId, std::move(view_token),
                                              "bouncing_circle_view"));

  // Root Node.
  PushCommand(&cmds, scenic::NewCreateEntityNodeCmd(kRootNodeId));
  PushCommand(&cmds, scenic::NewAddChildCmd(kViewId, kRootNodeId));

  // Background Material.
  PushCommand(&cmds, scenic::NewCreateMaterialCmd(kBgMaterialId));
  PushCommand(&cmds, scenic::NewSetColorCmd(kBgMaterialId, 0xf5, 0x00, 0x57,
                                            0xff));  // Pink A400

  // Background ShapeNode.
  PushCommand(&cmds, scenic::NewCreateShapeNodeCmd(kBgNodeId));
  PushCommand(&cmds, scenic::NewSetMaterialCmd(kBgNodeId, kBgMaterialId));
  PushCommand(&cmds, scenic::NewAddChildCmd(kRootNodeId, kBgNodeId));

  // Circle's Material.
  PushCommand(&cmds, scenic::NewCreateMaterialCmd(kCircleMaterialId));
  PushCommand(&cmds,
              scenic::NewSetColorCmd(kCircleMaterialId, 0x67, 0x3a, 0xb7,
                                     0xff));  // Deep Purple 500

  // Circle's ShapeNode.
  PushCommand(&cmds, scenic::NewCreateShapeNodeCmd(kCircleNodeId));
  PushCommand(&cmds,
              scenic::NewSetMaterialCmd(kCircleNodeId, kCircleMaterialId));
  PushCommand(&cmds, scenic::NewAddChildCmd(kRootNodeId, kCircleNodeId));

  session_->Enqueue(std::move(cmds));

  // Apply all the commands we've enqueued by calling Present. For this first
  // frame we call Present with a presentation_time = 0 which means it the
  // commands should be applied immediately. For future frames, we'll use the
  // timing information we receive to have precise presentation times.
  session_->Present(0, {}, {},
                    [this](fuchsia::images::PresentationInfo info) {
                      OnPresent(std::move(info));
                    });
}
```

## Getting our size (ViewProperties)

When we created our Session, we also created a binding to a `SessionListener`. `SessionListener`
is how we get events from Scenic, including size change events (sent as ViewProperties) and input
events.

For now, we only process ViewPropertiesChanged events. We get our size, and then
create `Shapes` (in this case, `Rectangle` for the background and `Circle` for the ball) for
the two nodes in our scene.
``` cpp
// Note: we implement the SessionListener protocol
class BouncingBallView : public fuchsia::ui::scenic::SessionListener {
    ...

    static bool IsViewPropertiesChangedEvent(
        const fuchsia::ui::scenic::Event& event) {
      return event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
             event.gfx().Which() ==
                 fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged;
    }

    // |fuchsia::ui::scenic::SessionListener|
    void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override {
      for (auto& event : events) {
        if (IsViewPropertiesChangedEvent(event)) {
          OnViewPropertiesChanged(
              event.gfx().view_properties_changed().properties);
        } else {
          // Unhandled event.
        }
      }
    }

    void OnViewPropertiesChanged(fuchsia::ui::gfx::ViewProperties vp) {
      view_width_ = (vp.bounding_box.max.x - vp.inset_from_max.x) -
                    (vp.bounding_box.min.x + vp.inset_from_min.x);
      view_height_ = (vp.bounding_box.max.y - vp.inset_from_max.y) -
                     (vp.bounding_box.min.y + vp.inset_from_min.y);

      // Position is relative to the View's origin system.
      const float center_x = view_width_ * .5f;
      const float center_y = view_height_ * .5f;

      // Build up a list of commands we will send over our Scenic Session.
      std::vector<fuchsia::ui::scenic::Command> cmds;

      // Background Shape.
      const int bg_shape_id = new_resource_id_++;
      PushCommand(&cmds, scenic::NewCreateRectangleCmd(bg_shape_id, view_width_,
                                                       view_height_));
      PushCommand(&cmds, scenic::NewSetShapeCmd(kBgNodeId, bg_shape_id));

      // We release the Shape Resource here, but it continues to stay alive in
      // Scenic because it's being referenced by background ShapeNode (i.e. the
      // one with id kBgNodeId). However, we no longer have a way to reference it.
      //
      // Once the background ShapeNode no longer references this shape, because a
      // new Shape was set on it, this Shape will be destroyed internally in
      // Scenic.
      PushCommand(&cmds, scenic::NewReleaseResourceCmd(bg_shape_id));

      // Translate the background node.
      constexpr float kBackgroundElevation = 0.f;
      PushCommand(&cmds, scenic::NewSetTranslationCmd(
                             kBgNodeId, {center_x, center_y, -kBackgroundElevation}));

      // Circle Shape.
      circle_radius_ = std::min(view_width_, view_height_) * .1f;
      const int circle_shape_id = new_resource_id_++;
      PushCommand(&cmds,
                  scenic::NewCreateCircleCmd(circle_shape_id, circle_radius_));
      PushCommand(&cmds, scenic::NewSetShapeCmd(kCircleNodeId, circle_shape_id));

      // We release the Shape Resource here, but it continues to stay alive in
      // Scenic because it's being referenced by circle's ShapeNode (i.e. the one
      // with id kCircleNodeId). However, we no longer have a way to reference it.
      //
      // Once the background ShapeNode no longer references this shape, because a
      // new Shape was set on it, this Shape will be destroyed internally in
      // Scenic.
      PushCommand(&cmds, scenic::NewReleaseResourceCmd(circle_shape_id));

      session_->Enqueue(std::move(cmds));

      // The commands won't actually get committed until Session.Present() is
      // called. However, since we're animating every frame, in this case we can
      // assume Present() will be called shortly.
    }

    ...
}
```

## Pushing new frames

If we want to push new content, we send more `Commands` and call `Present`.
However, we should use the information we received in `PresentationInfo` to determine what the
next presentation time will be. In this example, we continually request new frames to animate
the circle.

``` cpp
  void InitializeScene(zx::eventpair view_token) {

    // For the first call to present, use presentation_time = 0 since we don't
    // haven't gotten any timing information yet.
    session_->Present(0, {}, {},
                      [this](fuchsia::images::PresentationInfo info) {
                        // ** NEW CODE **: Call |OnPresent| here with the
                        // PresentationInfo.
                        OnPresent(std::move(info));
                      });
  }

  ...

  void OnPresent(fuchsia::images::PresentationInfo presentation_info) {
    uint64_t presentation_time = presentation_info.presentation_time;

    constexpr float kSecondsPerNanosecond = .000'000'001f;
    float t =
        (presentation_time - last_presentation_time_) * kSecondsPerNanosecond;
    if (last_presentation_time_ == 0) {
      t = 0;
    }
    last_presentation_time_ = presentation_time;

    std::vector<fuchsia::ui::scenic::Command> cmds;

    UpdateCirclePosition(t);
    const float circle_pos_x_absolute = circle_pos_x_ * view_width_;
    const float circle_pos_y_absolute =
        circle_pos_y_ * view_height_ - circle_radius_;

    // Translate the circle's node.
    constexpr float kCircleElevation = 8.f;
    PushCommand(&cmds, scenic::NewSetTranslationCmd(kCircleNodeId,
                           {circle_pos_x_absolute, circle_pos_y_absolute, -kCircleElevation}));
    session_->Enqueue(std::move(cmds));

    zx_time_t next_presentation_time = presentation_info.presentation_time +
                                       presentation_info.presentation_interval;
    session_->Present(next_presentation_time, {}, {},
                      [this](fuchsia::images::PresentationInfo info) {
                        OnPresent(std::move(info));
                      });
  }
```

## Input

We can also get input events (pointer events, keyboard events) from Scenic by
listening to Session events. For example, we can listen for a touch down and
then use that in the animation code to reset the ball's starting position:

``` cpp
// |fuchsia::ui::scenic::SessionListener|
void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override {
  for (auto& event : events) {
    if (IsViewPropertiesChangedEvent(event)) {
      OnViewPropertiesChanged(
          event.gfx().view_properties_changed().properties);
    } else if (IsPointerDownEvent(event)) {
      // *** NEW CODE ***
      pointer_down_ = true;
      pointer_id_ = event.input().pointer().pointer_id;
    } else if (IsPointerUpEvent(event)) {
      // *** NEW CODE ***
      if (pointer_id_ == event.input().pointer().pointer_id) {
        pointer_down_ = false;
      }
    } else {
      // Unhandled event.
    }
  }
}
```


## More examples

Congratulations! You've learned how to create a component that can display some cool content! Here are some next steps:

* Learn to use `BaseView`, which is a library that helps avoid some of the boilerplate we wrote here.
   * Library: [//src/lib/ui/base_view](https://fuchsia.googlesource.com/fuchsia/+/master/src/lib/ui/base_view)
   * Example: [Simplest App](https://fuchsia.googlesource.com/fuchsia/+/master/src/ui/examples/simplest_app/view.cc)
* Learn to use the Scenic cpp library, which creates an abstraction of a more object-oriented interface to Scenic `Resources`, rather than using `Commands` directly like we have here.
   * Library: [//sdk/lib/ui/scenic/cpp](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/lib/ui/scenic/cpp)
   * Example: [Spinning Square](https://fuchsia.googlesource.com/fuchsia/+/master/src/ui/examples/spinning_square/spinning_square_view.cc)
