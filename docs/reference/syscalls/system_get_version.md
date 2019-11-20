# zx_system_get_version

## NAME

<!-- Updated by update-docs-from-fidl, do not edit. -->

Get version string for system.

## SYNOPSIS

<!-- Updated by update-docs-from-fidl, do not edit. -->

```c
#include <zircon/syscalls.h>

zx_status_t zx_system_get_version(char* version, size_t version_size);
```

## DESCRIPTION

`zx_system_get_version()` fills in the given character array with a string
identifying the version of the Zircon system currently running.
The provided size must be large enough for the complete string
including its null terminator.

The version string is guaranteed to never require more than 64 bytes of storage
including the null terminator.

The first four characters identify the version scheme. An example of the string
returned is "git-8a07d52603404521038d8866b297f99de36f9162".

## RIGHTS

<!-- Updated by update-docs-from-fidl, do not edit. -->

TODO(ZX-2399)

## RETURN VALUE

`zx_system_get_version()` returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_BUFFER_TOO_SMALL**  *version_size* is too short.

## NOTES

## SEE ALSO


