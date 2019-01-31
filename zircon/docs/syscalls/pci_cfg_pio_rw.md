# zx_pci_cfg_pio_rw

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

pci_cfg_pio_rw - TODO(ZX-3106)

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_pci_cfg_pio_rw(zx_handle_t handle,
                              uint8_t bus,
                              uint8_t dev,
                              uint8_t func,
                              uint8_t offset,
                              uint32_t* val,
                              size_t width,
                              bool write);
```

## DESCRIPTION

TODO(ZX-3106)

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*handle* must have resource kind **ZX_RSRC_KIND_ROOT**.

## RETURN VALUE

TODO(ZX-3106)

## ERRORS

TODO(ZX-3106)

## SEE ALSO


TODO(ZX-3106)
