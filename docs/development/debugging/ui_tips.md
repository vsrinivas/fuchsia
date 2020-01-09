# UI Debugging Tips

For general debugging info see the [Fuchsia Debugging Workflow](/docs/development/debugging/debugging.md).

## Capture the Screen

### Take a Screenshot
A screenshot takes a screenshot of what is currently displayed on the Fuchsia device's screen.
It returns a 2D buffer.

From the Fuchsia device console, run:
`screencap /tmp/filename.ppm`

From your host workstation, run:
`fx scp [$(fx get-device-addr)]:/tmp/filename.ppm /tmp/filename.ppm`

### Take a Snapshot
A snapshot takes a 3D representation of what is currently displayed on the screen. It usually takes
longer to capture than a screenshot, and can be used to visualize issues with layout of 3D content.

From your host workstation, run:
`fx shell gltf_export > filename.gltf`

You can upload `filename.gltf` to any gltf viewer, such as this [online viewer](https://gltf-viewer.donmccurdy.com/).

### Dump the SceneGraph as Text
The [SceneGraph](/docs/concepts/graphics/scenic/scenic.md#scenic-resource-graph) as text is useful when you want to see all the resources, including non-visible elements such as transform matrices.

#### Dump the SceneGraph in Bugreport
The Fuchsia Bugreport contains the SceneGraph that is rendered to the screen. Capture it from your host workstation using the following commands:

`fx bugreport`
`unzip <bugreport output file>`

Then, look for Scenic’s info in the inspect file:

`cat inspect.json | less`

#### Dump the SceneGraph and all Scenic Resources
To capture all the Resources created, including those that are not currently attached to the main SceneGraph, you can use `dump-scenes`.
From your host workstation, run the following command:

`fx shell "cat /hub/c/scenic.cmx/*/out/debug/dump-scenes"`
