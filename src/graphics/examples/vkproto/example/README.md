# vkproto/example
See the parent vkproto/README.md for general information about vkproto.

## AGI Instrumentation
vkproto/example/main.cc is configured to request the `GraphicsSpyLayer`
layer in its instance creation.  Providing this layer service and
capability is done using the gapii.far package as built within the
AGI tree / build.

At the time of this writing, building gapii.far within the AGI build
for, e.g., arm64 is done using:

  - `CC=/usr/bin/gcc-8 bazel build --subcommands --config fuchsia_arm64 //gapii/fuchsia:gapii.far`

The output of this gapii.far build is created in the AGI build as:

  - `bazel-bin/gapii/fuchsia/gapii.far`

### Gapii Package Serving
gapii.far is served to a running Fuchsia instance using the following example
3 step process:

  1. Publish the gapii repository
    - `pm publish -a -C -f bazel-bin/gapii/fuchsia/gapii.far -repo ~/gapii-repo`
  2. Add the published repository
    - `ffx repository add-from-pm -r gapii-repo ~/gapii-repo`
  3. Register the repository
    - `ffx target repository register -r gapii-repo`


### Component Management
vkproto/example as defined must run in the `/core/agis:vulkan-trace`
collection that was specifically designed with the capability routing
needed for Vulkan tracable components that use AGI.

With the gapii.far archive published as described above, creating the
vkproto component underneath `/core/agis:vulkan-trace` requires the
following steps.

  1. `ffx component create /core/agis/vulkan-trace:vkproto fuchsia-pkg://fuchsia.com/vkproto-pkg#meta/vkproto-cmp.cm`
  2. `ffx component start /core/agis/vulkan-trace:vkproto`

Creating and starting the vkproto component will implicitly load the
gapii-server component needed to augment the Vulkan loader with the
directory capability needed to load the `GraphicsSpyLayer` layer.
