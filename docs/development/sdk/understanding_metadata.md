# Understanding SDK metadata

The manifest of the Core SDK is a JSON file that is described using
a [JSON schema](https://json-schema.org/latest/json-schema-core.html).
The goal of having a metadata based description of the SDK is to allow
automated processing of the SDK to integrate it into build environments.

It is expected that the contents and structure of the SDK will change over time
so care should be taken when interpreting the metadata during any transformations.
The source of truth for the structure of the metadata is always the files contains in
the `meta/schemas` directory of the SDK.

The source for the schema is found in [`build/sdk/meta`](/build/sdk/meta).

## Manifest structure

The [manifest](/build/sdk/meta/manifest.json) has the following required properties:

Property         |   Description
:----------------|:-------------:
|  arch          | Architecture targeted for this SDK. There is a host architecture and a list of target device architectures. |
| id             | Build id of the SDK. |
| parts          | The array of elements in the SDK. Each part has a type which is defined in `meta/schemas/<type>.json` |
| schema_version | The version of the schema for the metadata. This value should be verified when using an automated integration process to make sure the metadata is being interpreted correctly. |


## Element types
* [banjo_library](/build/sdk/meta/banjo_library.json)
* [cc_prebuilt_library](/build/sdk/meta/cc_prebuilt_library.json)
* [cc_source_library](/build/sdk/meta/cc_source_library.json)
* [dart_library](/build/sdk/meta/dart_library.json)
* [device_profile](/build/sdk/meta/device_profile.json)
* [documentation](/build/sdk/meta/documentation.json)
* [fidl_library](/build/sdk/meta/fidl_library.json)
* [host_tool](/build/sdk/meta/host_tool.json)
* [loadable_module](/build/sdk/meta/loadable_module.json)
* [sysroot](/build/sdk/meta/sysroot.json)
