#ifndef ZIRCON_SYSTEM_DEV_BUS_PCI_COMMON_H_
#define ZIRCON_SYSTEM_DEV_BUS_PCI_COMMON_H_

#include <ddk/debug.h>

#define pci_tracef(...) zxlogf(TRACE, "pci: " __VA_ARGS__)
#define pci_errorf(...) zxlogf(ERROR, "pci: " __VA_ARGS__)
#define pci_infof(...) zxlogf(INFO, "pci: " __VA_ARGS__)

#endif // ZIRCON_SYSTEM_DEV_BUS_PCI_COMMON_H_
