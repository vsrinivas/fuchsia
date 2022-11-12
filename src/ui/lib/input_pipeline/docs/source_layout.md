# input_pipeline > Source layout

Reviewed on: 2022-03-22

All modules are in the `src` directory (parallel to this `docs` directory), and are referenced from `src/lib.rs`.
* `input_pipeline.rs` defines the top-level structs needed by components that
  wish to instantiate an input pipeline. These are `input_pipeline::InputPipeline`,
  and `input_pipeline::InputPipelineAssembly`.
* `input_device.rs` defines the types (e.g. `struct`s, `trait`s, `enum`s) which
  represent input devices and input events within the library.
* The various `binding`s define the `struct`s that read from input devices exposed by
  `/dev/class/input-report`. Each binding reads `fuchsia.input.report.InputReport`
  FIDL messages, and translates the Input Reports into `input_device::InputEvent`s.
* The `focus_listener` module receives focus change notifications from Scenic, and
  dispatches relevant updates to FIDL peers (e.g. the shortcut manager) that need
  to know which `fuchsia.ui.views.View` has keyboard focus.
* Various other modules implement input pipeline stages. A stage reads `InputEvent`s,
  possibly decorating them, interpreting them, and/or transforming them to `fuchsia.input.InputEvent`s. In the last case, the stage typically sends the message to a FIDL peer.
  * Stages which implement the `InputHandler` trait are called handlers.
  * All handlers are stages, but not all stages are handlers.
  * Some stages are documented in the [`stages` folder](stages/).
* Other modules provide testing support, or other utilities.
