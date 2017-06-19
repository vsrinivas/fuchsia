# Fuchsia Package Metadata

The Fuchsia package format contains an extensive metadata directory. This
document describes the metadata extensions that are understood by Fuchsia
itself.

See [https://fuchsia.googlesource.com/pm#Structure-of-a-Fuchsia-Package] for
more information about where these files appear in a package.

## metadata

See [https://fuchsia.googlesource.com/pm#metadata]

## contents

See [https://fuchsia.googlesource.com/pm#contents]

## signature

See [https://fuchsia.googlesource.com/pm#signature]

## sandbox

The sandbox file controls the environment in which the contents of the package
execute. Specifically, the file controls which resources the package can access
during execution.

The sandbox file is a JSON object with the following schema:

```
{
    "type": "object",
    "properties": {
        "dev": {
            "type": "array",
            "items": {
                "type": "string"
            }
        },
        "features": {
            "type": "array",
            "items": {
                "type": "string"
            }
        }
    }
}
```

The `dev` array contains list of well-known device paths that are provided to
the application. For example, if the string `class/input` appears in the `dev`
array, then `/dev/class/input` will appear in the namespaces of applications
loaded from the package.

The `features` array contains a list of well-known features that the package
wishes to use. Including a feature in this list is a request for the environment
in which the contents of the package execute to be given the resources required
to use that feature.

The set of currently known features are as follows:

- `vulkan`, which requests access to the resources required to use the Vulkan
  graphics interface.

See [sandboxing.md] for more information about sandboxing.
