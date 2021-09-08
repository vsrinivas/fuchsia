# Prototype Vulkan Validation Layer package

This prototype allows applications to depend on the Vulkan validation layers
dynamically, rather than including the layers in their package. A package will
always load the exact same version of the layers that it was compiled with.

## Usage

This prototype requires that the client uses CFv2.

1. Depend on the `//src/lib/vulkan/validation-layer-package:validation-client`
  target in your component.
2. Include the `vulkan/client.shard.cml` CML shard.
3. Add `/vulkan_validation_pkg/data/vulkan/explicit_layer.d` to the layer path, in one of several ways:

    * Set the `VK_LAYER_PATH=/vulkan_validation_pkg/data/vulkan/explicit_layer.d` [environment variable][environ].
    * Set `"override_paths": ["/vulkan_validation_pkg/data/vulkan/explicit_layer.d"]` in [VkLayer_override.json](#override)

4. Enable the `VK_LAYER_KHRONOS_validation` layer, in one of several ways:

    * Pass it to `vkCreateInstance`
    * Set the `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` [environment variable][environ].
    * Set `"component_layers": [ "VK_LAYER_KHRONOS_validation" ]` in [VkLayer_override.json](#override).

5. Add `//src/lib/vulkan/validation-layer` package using `fx set`.

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

This can be placed in any of these directories:

* `/pkg/data/vulkan/implicit_layer.d/VkLayer_override.json`
* `/config/vulkan/implicit_layer.d/VkLayer_override.json`
* `<x>/implicit_layer.d/VkLayer_override.json` if the `XDG_CONFIG_DIRS=<x>`
  [environment variable][environ] is set. This can be used to read the file from
  `/data` or `/cache`, which can be written at runtime.

[environ]: docs/concepts/components/v2/elf_runner.md#environment_variables
