# Vulkan Validation Package

Vulkan validation package, henceforth VVP, allows applications to depend on the Vulkan validation
layers dynamically, rather than including the layers in their package. A package will always load
the exact same version of the layers that it was compiled with. Further, it will only load the
shared library required for Vulkan validation from a package management repository onto the Fuchsia
device if it is configured to do so. A key element of this design is the ability, with few
configuration steps, to configure a Vulkan components to use or not use the Vulkan validation layers
and only to incur the on-Fuchsia-device storage cost of storing the validation layers if they are in
fact in use.

## Realm Anatomy

VVP defines 2 new components and a new capability along with its routing for Vulkan applications to
`use`. As such, VVP effectively defines a new component [realm][realm]. In the simplest use case,
use of this VVP scheme results in a realm with 2 packages and 3 components. The components are
arranged as an ancestry that is 3 levels deep as follows: <br>![realm][realm-anatomy]<br>

The solid line arrows depict parent-child component relationships with the arrow pointing towards
the child. The dashed line indicates a directory capability provided by the validation server to the
application component that gives the application component, and therefore the loader, visibility
into the validation servers package contents.

The first package is that of the application being augmented to use the Vulkan validation layers and
it contains 2 components:

   1. The original application component which is amended to include the
      //src/lib/vulkan/validation-layer-package/application.shard.cml as part of its component
      manifest.

   2. The new client-side component, validation-client as introduced by this scheme, that serves to
      allow specific version hashing to always pair the matching version of the validation layer
      shared library with the version your application was built against.



The second package is the "validation-server-package". It contains a single component with an
executable, a shared library and `.json` resources.  The layer server executable serves its entire
package contents as the `validation_server_pkg` directory capability. This directory capability
contains the layer .json files under the `data/vulkan/explicit_layer.d/` path and the shared
libraries in  `lib/`.

Effectively, this provides the application (and therefore the loader), with all the resources it
needs to implicitly enable the Vulkan validation layers and guarantees that the correct version of
the validation layer `.so` is always loaded when it is used.

## Usage

This prototype requires that the client uses CFv2.

1. Have your application's package depend on the
   `//src/lib/vulkan/vulkan-validation-pkg:validation-client` component.
2. Include the `//src/lib/vulkan/vulkan-validation-pkg/application.shard.cml` CML shard in your
   application's component manifest.
3. Add `/vulkan_validation_pkg/data/vulkan/explicit_layer.d` to the layer path, in one of several
   ways:

    * Set the `VK_LAYER_PATH=/vulkan_validation_pkg/data/vulkan/explicit_layer.d`
      [environment variable][environ].
    * Set `"override_paths": ["/vulkan_validation_pkg/data/vulkan/explicit_layer.d"]` in
      [VkLayer_override.json](#override)

4. Enable the `VK_LAYER_KHRONOS_validation` layer, in one of several ways:

    * Pass it to `vkCreateInstance`
    * Set the `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` [environment variable][environ].
    * Set `"component_layers": [ "VK_LAYER_KHRONOS_validation" ]` in
      [VkLayer_override.json](#override).

5. Add the `//src/lib/vulkan/vulkan-validation-pkg` package using `fx set`.

When configured as described above, the loading of the validation layer from the package repository
to the Fuchsia device can be controlled by performing or reverting the change described in step 3
above.

### VkLayer_override.json {#override}

Here's an example VkLayer_override.json file that automatically finds and loads
the validation layers:

```json
{
    "file_format_version": "1.1.2",
    "layer": {
        "api_version": "1.2.182",
        "app_keys": [],
        "blacklisted_layers": [],
        "component_layers": [
            "VK_LAYER_KHRONOS_validation"
        ],
        "description": "LunarG Override Layer",
        "disable_environment": {
            "DISABLE_VK_LAYER_LUNARG_override": "1"
        },
        "implementation_version": "1",
        "name": "VK_LAYER_LUNARG_override",
        "override_paths": [
            "/vulkan_validation_pkg/data/vulkan/explicit_layer.d"
        ],
        "type": "GLOBAL"
    }
}
```

VkLayer_override.json can be placed at any of these paths:

* `/pkg/data/vulkan/implicit_layer.d/VkLayer_override.json`
* `/config/data/vulkan/implicit_layer.d/VkLayer_override.json`
* `<x>/implicit_layer.d/VkLayer_override.json` if the `XDG_CONFIG_DIRS=<x>`
  [environment variable][environ] is set. This can be used to read the file from
  `/data` or `/cache`, which can be written at runtime.

[environ]: docs/concepts/components/v2/elf_runner.md#environment_variables

[realm]: https://fuchsia.dev/fuchsia-src/concepts/components/v2/realms?hl=en
[realm-anatomy]: images/vulkan_validation_pkg.svg
