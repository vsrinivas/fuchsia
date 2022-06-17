# Views

View is a Carnelian abstraction representing something a user can see and interact with.

Carnelian developers create a view by implementing the  [`ViewAssistant`](https://fuchsia-docs.firebaseapp.com/rust/carnelian/trait.ViewAssistant.html)
trait.

[`create_view_assistant()`](https://fuchsia-docs.firebaseapp.com/rust/carnelian/app/trait.AppAssistant.html#method.create_view_assistant) or [`create_view_assistant_with_parameters()`](https://fuchsia-docs.firebaseapp.com/rust/carnelian/app/trait.AppAssistant.html#method.create_view_assistant_with_parameters) trait methods.

[`ViewAssistant`](https://fuchsia-docs.firebaseapp.com/rust/carnelian/trait.ViewAssistant.html) has two primary responsibilities; handling user input and rendering contents to be shown on the screen.

The [`render()` trait method](https://fuchsia-docs.firebaseapp.com/rust/carnelian/trait.ViewAssistant.html#method.render) is called when it is time for Carnelian to produce pixels.

Pixels are produced by creating a [`Composition`](https://fuchsia-docs.firebaseapp.com/rust/carnelian/render/struct.Composition.html),
if one doesn't already exist, and then adding or removing the [`Layers`](https://fuchsia-docs.firebaseapp.com/rust/carnelian/render/struct.Layer.html) contained.

Much of the boilerplate for pixel production can be handled by a [`Scene`](https://fuchsia-docs.firebaseapp.com/rust/carnelian/scene/scene/struct.Scene.html). See the [Scenes](./scenes.md) chapter of this book for more details about using scenes.

## Notes

### Multiple Views

A Carnelian application is capable of hosting multiple views in a single process, but this doesn't occur that often. At the moment, only applications running on the display controller will ever be asked to create more than one view, and only when there is more than one display plugged into the Fuchsia device.

A Carnelian application can ask for [additional views](https://fuchsia-review.googlesource.com/c/fuchsia/+/669892). This is currently supported only on core, and only one of the views hosted on a display is visible at at time.

The implementation of this feature on the Flatland flavor of Scenic would allow Carnelian to be used to implement a window manager.

## Concerns
