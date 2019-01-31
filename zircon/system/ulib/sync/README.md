This library is used directly in libc.  So it cannot use most high-level
facilities.  In particular, <zircon/assert.h> is not permitted because it
uses printf.  It's also important to restrict the (non-hidden) symbols
used to only standard C symbols and the reserved namespace (external
linkage symbols starting with `_`).  The Zircon system call API can be
used freely, but only via the `_zx_` names and not the `zx_` names.
