// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi/types.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/driver-binding.h>
#include <efi/protocol/usb-io.h>

#include <xefi.h>
#include <stdio.h>

EFIAPI efi_status MyDriverSupported(
    efi_driver_binding_protocol* self, efi_handle ctlr,
    efi_device_path_protocol* path) {

    efi_usb_device_descriptor dev;
    efi_usb_io_protocol* usbio;
    efi_status r;

    r = gBS->OpenProtocol(ctlr, &UsbIoProtocol,
                          (void**)&usbio, self->DriverBindingHandle,
                          ctlr, EFI_OPEN_PROTOCOL_BY_DRIVER);

    if (r == 0) {
        if (usbio->UsbGetDeviceDescriptor(usbio, &dev)) {
            return EFI_UNSUPPORTED;
        }
        printf("Supported? ctlr=%p vid=%04x pid=%04x\n",
               ctlr, dev.IdVendor, dev.IdProduct);
        gBS->CloseProtocol(ctlr, &UsbIoProtocol,
                           self->DriverBindingHandle, ctlr);
        return EFI_SUCCESS;
    }
    return EFI_UNSUPPORTED;
}

EFIAPI efi_status MyDriverStart(
    efi_driver_binding_protocol* self, efi_handle ctlr,
    efi_device_path_protocol* path) {
    efi_status r;

    efi_usb_io_protocol* usbio;

    printf("Start! ctlr=%p\n", ctlr);

    r = gBS->OpenProtocol(ctlr, &UsbIoProtocol,
                          (void**)&usbio, self->DriverBindingHandle,
                          ctlr, EFI_OPEN_PROTOCOL_BY_DRIVER);

    // alloc device state, stash usbio with it
    // probably attached to a protocol installed on a child handle

    if (r) {
        printf("OpenProtocol Failed %lx\n", r);
        return EFI_DEVICE_ERROR;
    }
    return EFI_SUCCESS;
}

EFIAPI efi_status MyDriverStop(
    efi_driver_binding_protocol* self, efi_handle ctlr,
    size_t count, efi_handle* children) {

    printf("Stop! ctlr=%p\n", ctlr);

    // recover device state, tear down

    gBS->CloseProtocol(ctlr, &UsbIoProtocol,
                       self->DriverBindingHandle, ctlr);
    return EFI_SUCCESS;
}

static efi_driver_binding_protocol MyDriver;

void InstallMyDriver(efi_handle img, efi_system_table* sys) {
    efi_boot_services* bs = sys->BootServices;
    efi_handle* list;
    size_t count, i;
    efi_status r;

    MyDriver.Supported = MyDriverSupported;
    MyDriver.Start = MyDriverStart;
    MyDriver.Stop = MyDriverStop;
    MyDriver.Version = 32;
    MyDriver.ImageHandle = img;
    MyDriver.DriverBindingHandle = img;
    r = bs->InstallProtocolInterface(&img, &DriverBindingProtocol,
                                     EFI_NATIVE_INTERFACE, &MyDriver);
    if (r) {
        printf("DriverBinding failed %lx\n", r);
        return;
    }

    // For every Handle that supports UsbIoProtocol, try to connect the driver
    r = bs->LocateHandleBuffer(ByProtocol, &UsbIoProtocol, NULL, &count, &list);
    if (r == 0) {
        for (i = 0; i < count; i++) {
            r = bs->ConnectController(list[i], NULL, NULL, false);
        }
        bs->FreePool(list);
    }
}

void RemoveMyDriver(efi_handle img, efi_system_table* sys) {
    efi_boot_services* bs = sys->BootServices;
    efi_handle* list;
    size_t count, i;
    efi_status r;

    // Disconnect the driver
    r = bs->LocateHandleBuffer(ByProtocol, &UsbIoProtocol, NULL, &count, &list);
    if (r == 0) {
        for (i = 0; i < count; i++) {
            r = bs->DisconnectController(list[i], img, NULL);
        }
        bs->FreePool(list);
    }

    // Unregister so we can safely exit
    r = bs->UninstallProtocolInterface(img, &DriverBindingProtocol, &MyDriver);
    if (r)
        printf("UninstallProtocol failed %lx\n", r);
}

EFIAPI efi_status efi_main(efi_handle img, efi_system_table* sys) {
    xefi_init(img, sys);

    printf("Hello, EFI World\n");

    InstallMyDriver(img, sys);

    // do stuff

    RemoveMyDriver(img, sys);

    xefi_wait_any_key();
    return EFI_SUCCESS;
}
