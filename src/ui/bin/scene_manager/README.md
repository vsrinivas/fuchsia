# Scene Manager

The Scene Manager component creates and configures a Scenic scene graph, and an input pipeline
on launch.

Clients can connect to `fuchsia.session.scene.Manager` to interact with the scene graph. Currently,
clients can set the root view of the scene, and focus views.

The scene manager also currently provides a default input pipeline implementation for the workstation
product.