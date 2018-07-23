# zx_system_get_version

## NAME

system_get_version - get version string for system

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_system_get_version(char version[], size_t version_size);
```

## DESCRIPTION

**system_get_version**() fills in the given character array with a string
identifying the version of the Zircon system currently running.
The provided size must be large enough for the complete string
including its null terminator.


## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**system_get_version**() returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_BUFFER_TOO_SMALL**  *version_size* is too short.

## NOTES

## SEE ALSO
