# The Vulkan Loader service

This service is responsible for providing Vulkan [ICDs][ICD] to applications that use vulkan.

## ICD identification

The vulkan loader waits for GPU devices to appear, then queries them for the
URLs of ICD components that can be used with them. The exact query depends on
the type of GPU device:

* /dev/class/gpu - [fuchsia.gpu.magma/Device.GetIcdList][GetIcdList] is called
    on the device.
* /dev/class/goldfish-pipe: The ICD URL is hardcoded to be
    fuchsia-pkg://fuchsia.com/libvulkan_goldfish#meta/vulkan.cm

More types of GPU hardware devices may be supported in the future.

## ICD<->loader interface

ICD are made available to the loader as [CFv2 components][component]. An ICD
component must expose a `contents` directory containing an arbitrary
directory tree containing a shared library, as well as a `metadata` directory
containing a single `metadata.json` file.

An ICD is generally contained in its own [package]. In that case, the
`contents` directory would be the root of the package, and the `metadata`
directory would be the `meta/metadata/` directory in the package. The loader
doesn't enforce this layout, however.

`metadata.json` and `manifest.json` should ideally be stored under the [`meta`
directory in the package][meta-far], since that directory is most efficient at
storing small files.

### ICD shared libraries

ICD shared libraries should match the [Vulkan ICD ABI][ICD]. ICDs are
executable shared libraries and can be placed in most subdirectories (not `/bin`)
of the package.

### Component manifest

The Vulkan loader supplies an `icd_runner` [runner] to simplify the creation
of an ICD component from a package. The ICD package must contain a [component
manifest][component-manifest] `.cml` file similar to the following:


```json
{
    include: [ "syslog/client.shard.cml" ],
    program: {
        runner: "icd_runner",
    },
    capabilities: [
        {
            directory: "contents",
            rights: [ "rx*" ],
            path: "/pkg",
        },
    ],
    expose: [
        {
            directory: "contents",
            from: "self",
            rights: [ "rx*" ],
        },
        {
            directory: "contents",
            from: "self",
            as: "metadata",
            rights: [ "r*" ],
            subdir: "meta/metadata",
        },
    ],
}
```

The "icd_runner" runner automatically exports the directories from the
package. This CML assumes the metadata is at `meta/metadata/metadata.json` in the
package. That can be changed by modifying the `subdir` property of the
`metadata` entry.

The ICD component may also use the elf runner, but the only service available
to it is `fuchsia.logger.LogSink`.

### metadata.json

metadata.json is a single JSON file that describes the ICD to the loader. Example:

```json
{
    "file_path": "lib/libvulkan_example.so",
    "version": 1,
    "manifest_path": "meta/icd.d/libvulkan_example.json"
}
```

* `version` must be 1 for this metadata version.
* `file_path` is the location of the ICD shared library relative to the exposed `contents` directory.
* `manifest_path` is the location of the [Khronos ICD manifest json file][loaderinterface] relative
    to the exposed `contents` directory.

## Debugging

The loader service exposes inspect data (under `core/vulkan_loader`) about its
current state and what components it has loaded. `manifest-fs` and `device-fs`
are also exposed through the [hub][hub] at
`/hub-v2/children/core/children/vulkan_loader/out/debug/`. The loader
service must be launched first; one way to do that is using [ffx component
start][ffx-start].

[GetIcdList]: https://fuchsia.dev/reference/fidl/fuchsia.gpu.magma#Device.GetIcdList
[VMO]: /docs/glossary.md#virtual-memory-object
[ICD]: /docs/concepts/system/abi/system.md#vulkan-icd
[runner]: /docs/concepts/components/v2/capabilities/runners.md
[component]: /docs/glossary.md#component
[package]: /docs/concepts/packages/package.md
[component-manifest]: /docs/concepts/components/v2/component_manifests.md
[loaderinterface]: https://github.com/KhronosGroup/Vulkan-Loader/blob/master/loader/LoaderAndLayerInterface.md
[meta-far]: /docs/concepts/packages/package.md#meta-far
[hub]: /docs/concepts/components/v2/hub.md
[ffx-start]: /docs/development/sdk/ffx/start-a-component-during-development.md#start-a-component