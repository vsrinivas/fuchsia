# framebuffer

**NOTE**: This is not a general purpose library. Clients should be aware of this
library's limitations before using it.

This library provides a simple framebuffer abstraction on top of the Zircon display
APIs. Applications can use this library to provide a low level UI on systems which lack
a normal UI framework. **Important**: When a normal UI framework is running, this
library will not be able to bind to the display.

The library supports two modes of operation: a single buffer mode where the library
initializes the display hardware with a single, linear VMO that clients can then
render to in place, and a page flip mode where the client can provide multiple VMO
backed images and flip between them. Clients are responsible for ensuring buffers
are kept coherent with main memory.

The library does not expose support for multiple monitors, hotplugging, modesetting,
or 2D hardware composition. Applications which want these features should use the Zircon
display APIs directly.
