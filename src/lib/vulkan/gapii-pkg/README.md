# Gapii Packaging on Fuchsia

---

## Abstract

AGI (Android GPU Inspector) is an application that was written for Android to perform system
profiling, gpu counter gathering and Vulkan command tracing. On Fuchsia, the Vulkan application to
be traced will be henceforth referred to as the **Vulkan tracable component**.

To perform Vulkan command tracing **gapii-pkg**, as defined in this project, must be installed on
the Fuchsia device. Gapii-pkg includes the gapii shared library that works within the Vulkan layer
framework to intercept Vulkan commands for profiling, debugging and replaying of Vulkan commands.
The gapii shared library also serves as the device-side communication endpoint for AGI host
application to communicate with the Vulkan tracable component.

For version consistency, gapii-pkg is maintained hermetically as part of the AGI host application
build. AGI is built using Bazel so it's natural, given our hermetic goal of keeping gapii and the
host side application in version lockstep, to build gapii-pkg inside of the AGI bazel build. This
requires augmenting AGI's bazel toolchain handling and rules to include the requirements to
cross-compile gapii for Fuchsia and to package gapii for installation on a Fuchsia device.

To accommodate Vulkan's loader requirements on Fuchsia and Fuchsia's capability-based access to its
file system, the final build product that must be produced by the AGI bazel build is a Fuchsia
package `.far` file containing:

- The gapii interposing shared library `.so`
- A `.json` file describing Vulkan layer override information
- A namespace service binary

The role of the namespace service binary is to extend the namespace of the Vulkan traceable
component to allow gapii to be loaded by the Vulkan loader using metalayer semantics.
<br><br>
## Realm Anatomy

Gapii-pkg defines a single package with a single component, gapii-server-component.
Gapii-server-component is configured by its manifest to serve its package namespace along with all
of the json files and shared library contents within to the Vulkan traceable component that needs
them.

For a Vulkan traceable component to use gapii-pkg, it must include `application.shard.cml`, as
defined in this project, as part of its manifest. Including this file in its manifest will result in
the gapii-pkg getting ephemerally loaded on to the Fuchsia device when the loader executing in the
Vulkan traceable component reads from the exported gapii-pkg directory. Further, the inclusion of
gapii-pkg will result in its namespace being augmented with access to the gapii shared library and
json files required by the Vulkan loader / layer system.

<br><image src="images/agi_gapii_packaging.svg">
<b>
<center>Figure 1: gapii-pkg component relationships, capability routing and lifecycle</center>
</b>
</image><br>

## Vulkan Metalayers on Fuchsia

Vulkan metalayers are best described [here][metalayers].

The operative Vulkan metalayer that will allow the Fuchsia Vulkan loader to load gapii in a
canonical way is [VK_LAYER_LUNARG_override][override].

Usage of this metalayer requires configuring VkLayer_override.json similar to the following example:

### VkLayer_override.json

```
{
    # Layer Key - version of this metalayer manifest file.
    "file_format_version": "1.1.2",

    # Layer Key
    "layer": {
        # Layer Key
        "name": "VK_LAYER_LUNARG_override",

        # Layer Key - Vulkan version |component_layers| were built against.
        "api_version": "1.2.198",

        # Layer Key - version of the layer
        "implementation_version": "1",

        # VK_LAYER_LUNARG_override - paths to the Vulkan applications that this override
        # should act upon.  If missing, this override metalayer is applicable to all
        # Vulkan applications.
        "app_keys": [],

        # VK_LAYER_LUNARG_override - this is not documented by LunarG.  Appears to
        # be a hard list of exceptions to prevent overrides from affecting specific layers.
        "blacklisted_layers": [],

        # Metalayer Key - identifies constituent layers for this metalayer
        "component_layers": [
            "VK_LAYER_GOOGLE_gapii"
        ],

        # Layer Key
        "description": "LunarG Override Layer",

        # Implicit Layer Key - environment variable to disable
        "disable_environment": {
            "DISABLE_VK_LAYER_LUNARG_override": "1"
        },

        # VK_LAYER_LUNARG_override - where to look for override or additional layers.
        "override_paths": [
            "/gapii_pkg/data/vulkan/explicit_layer.d"
        ],

        # Layer Key - INSTANCE or GLOBAL
        "type": "GLOBAL"
    }
}
```

It's worth noting that despite the name and implied semantics of the `VK_LAYER_LUNARG_override`
layer mechanism, gapii makes use of this metalayers capability to **add** an additional layer rather
than **override** specific layers.
<br><br>

### Packaging of VkLayer_override.json

`VkLayer_override.json` must be included as part of the Vulkan traceable component's package. The
Fuchsia loader performs implicit layer loading of the VK_LAYER_LUNARG_override metalayer by using
the following configuration:

- Install `/pkg/data/vulkan/implicit_layer.d/VkLayer_override.json`

  - Using the GN build system, define and have the Vulkan traceable component depend on a `resource`
    target as follows:

  ```
  resource("override_resource") {
      sources = [ "VkLayer_override.json" ]
      outputs = [ "/pkg/data/vulkan/implicit_layer.d/{{source_file_part}}" ]
  }
  ```
<br><br>

## Namespace Augmentation

In order to make use of VK_LAYER_LUNARG_override and the `.json` file defined above, a Vulkan
traceable component needs its filesystem namespace augmented such that when the Vulkan loader
implicitly loads layers, it has visibility and read permission into the `override_paths` defined.

Augmenting the namespace is performed by:

- Creating an executable that can provide the namespace augmentation service
- Exposing the service as a capability from the server
- Using the service in the Vulkan traceable component via inclusion of `application.shard.cml` in
  its component manifest

Use of the namespace service capability will implicitly cause the Fuchsia component manager to load
the `.far` containing gapii (as defined above) along with this namespace service binary. The
dependency will also cause the service to be invoked thus providing the namespace augmentation.

The code and specific details of the namespace augmentation service can be found
[here][layer-server].
<br><br>

## Enabling or Disabling Vulkan Tracing

Enabling Vulkan tracing is done by defining the `override_paths` key in the Vulkan traceable
component's [VkLayer_override.json](#vklayer_overridejson) to:
`/gapii_pkg/data/vulkan/explicit_layer.d`.

Disabling Vulkan tracing is done by leaving the `override_paths` key empty in the Vulkan traceable
component's VkLayer_override.json.

[metalayers]: https://chromium.googlesource.com/external/github.com/KhronosGroup/Vulkan-Loader/+/refs/tags/sdk-1.1.108.0/loader/LoaderAndLayerInterface.md#layer-manifest-file-format
[layer-server]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/vulkan/vulkan-validation-pkg/validation_server.cc
[override]: https://www.lunarg.com/wp-content/uploads/2021/09/Vulkan-Layer-Symbiosis-within-the-Vulkan-Ecosystem.pdf
