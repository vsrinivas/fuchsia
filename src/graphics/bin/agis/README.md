# Agis
Android GPU Inspector repository fidl service that maintains registered
Vulkan Traceable Components (vtcs).

## Layers
There are 4 primary software layers / modules in the design of AGI/Agis
on Fuchsia. The first two are host modules that communicate using a
unix domain socket.  The second (ffx) and fourth (vtc) layers
communicate using a Zircon socket that is instantiated and vended by
from the third (Agis) layer.

*Host*

  1. AGI application
  2. ffx plug-in / ffx daemon

---

*Device*

  3. Agis service
  4. Vulkan Traceable Component

## Files
| File | Description |
| ---- | ----------- |
| `agis.cc` | Fidl service |
| `test.cc` | Hermetic fidl service test that instances its own Agis service |
| `vtc_test.cc` | Utility app for manually testing socket connectivity that mocks an actual vtc using the core system agis service rooted as /core/agis/vulkan_trace:vtc-test. |

## Further Reading
[AGI On Fuchsia Design][Design]


[Design]: https://docs.google.com/document/d/1RbY3c23fu7hZPq7cW5cbzST4YICC6rRnFddHgwRpJRc/edit?usp=sharing
