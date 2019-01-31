# zx_pci_get_bar

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

pci_get_bar - TODO(ZX-3106)

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_pci_get_bar(zx_handle_t handle,
                           uint32_t bar_num,
                           zx_pci_bar_t* out_bar,
                           zx_handle_t* out_handle);
```

## DESCRIPTION

TODO(ZX-3106)

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*handle* must be of type **ZX_OBJ_TYPE_PCI_DEVICE** and have **ZX_RIGHT_READ** and have **ZX_RIGHT_WRITE**.

## RETURN VALUE

TODO(ZX-3106)

## ERRORS

TODO(ZX-3106)

## SEE ALSO


TODO(ZX-3106)
