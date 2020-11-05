# Hardware Display Controller Provider components (fake and real)

The `BUILD.gn` in this directory generates two component packages, `hardware-display-controller-provider` and `fake-hardware-display-controller-provider`.

Both of these publish the `fuchsia.hardware.display.Provider` service.  In general, the "fake" service is the one you'll want to use (see below: **Why use the "fake" service, not the "real" one?**).


## Purpose

The goal of `fake-hardware-display-controller-provider` is to enable hermetic tests involving Scenic, which by default serves itself an implementation of `fuchsia.hardware.display.Provider`, which acts as a proxy to the same service provided via `/dev/class/display`.

The service provided by `/dev/class/display` connects to the real hardware (allowing pixels to show up on the screen), but has an important limitation: it supports only a single client connection at any given time.  Consequently, two instances of Scenic cannot be run simultaneously, at least not if both are connected/proxied via `/dev/class/display`.

This is where `fake-hardware-display-controller-provider` comes into play.  A hermetic test can spawn instances of both the fake service and Scenic, the latter using the fake service instead of its own default implementation; see the **Usage** section below.  Multiple such Scenic instances can coexist, because they are no longer competing for a single resource.


## Usage

To use the fake service in a test, it must be included in the list of `injected-services` in the `fuchsia.test` facet of the test-packages `.cmx` file.  For example, this is what `flatland_renderer_unittests.cmx` looked like at the time this was written:

```
{
    "facets": {
        "fuchsia.test": {
            "injected-services": {
                "fuchsia.hardware.display.Provider": "fuchsia-pkg://fuchsia.com/fake-hardware-display-controller-provider#meta/hdcp.cmx",
                "fuchsia.tracing.provider.Registry": "fuchsia-pkg://fuchsia.com/trace_manager#meta/trace_manager.cmx"
            },
            "system-services": [
                "fuchsia.sysmem.Allocator",
                "fuchsia.vulkan.loader.Loader"
            ]
        }
    },
    "program": {
        "binary": "test/flatland_renderer_unittests"
    },
    "sandbox": {
        "features": [
            "vulkan"
        ],
        "services": [
            "fuchsia.hardware.display.Provider",
            "fuchsia.intl.PropertyProvider",
            "fuchsia.logger.LogSink",
            "fuchsia.sysmem.Allocator",
            "fuchsia.tracing.provider.Registry",
            "fuchsia.vulkan.loader.Loader"
        ]
    }
}
```

NOTE: `fake-hardware-display-controller` makes a connection to the same `fuchsia.sysmem.Allocator` that is made available to other components in the test environment.  This allows the fake display controller to collaborate with Scenic, Vulkan, etc. to negotiate the allocation of memory that meets everyone's requirements.

## Why use the "fake" service, not the "real" one?

Much like the default implementation that Scenic serves to itself, the "real" service acts as a proxy to `/dev/class/display`.  In fact, the "real" service and Scenic's default implementation share the same code: `//src/ui/lib/display:hdcp_service`.

Consequently, replacing Scenic's default implementation with the "real" service is a functional no-op, except for the additional costs of running the service in a separate process.

The reason that the "real" service exists is historical, to verify that Scenic's implementation of `fuchsia.hardware.display.Provider` is indeed replacable.  It could be argued that it should be deleted, although it could conceivably be used by a non-Scenic client that is denied direct access to `/dev/class/display`.
