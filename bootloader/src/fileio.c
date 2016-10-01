// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi/system-table.h>
#include <efi/types.h>
#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>

#include <stdio.h>
#include <xefi.h>

EFIAPI efi_status efi_main(efi_handle img, efi_system_table* sys) {
    efi_loaded_image_protocol* loaded;
    efi_status r;

    xefi_init(img, sys);

    printf("Hello, EFI World\n");

    r = xefi_open_protocol(img, &LoadedImageProtocol, (void**)&loaded);
    if (r)
        xefi_fatal("LoadedImageProtocol", r);

    printf("Img DeviceHandle='%ls'\n", xefi_handle_to_str(loaded->DeviceHandle));
    printf("Img FilePath='%ls'\n", xefi_devpath_to_str(loaded->FilePath));
    printf("Img Base=%p Size=%lx\n", loaded->ImageBase, loaded->ImageSize);

    efi_simple_file_system_protocol* sfs;
    r = xefi_open_protocol(loaded->DeviceHandle, &SimpleFileSystemProtocol, (void**)&sfs);
    if (r)
        xefi_fatal("SimpleFileSystemProtocol", r);

    efi_file_protocol* root;
    r = sfs->OpenVolume(sfs, &root);
    if (r)
        xefi_fatal("OpenVolume", r);

    efi_file_protocol* file;
    r = root->Open(root, &file, L"README.txt", EFI_FILE_MODE_READ, 0);

    if (r == EFI_SUCCESS) {
        char buf[512];
        size_t sz = sizeof(buf);
        efi_file_info* finfo = (void*)buf;
        r = file->GetInfo(file, &FileInfoGuid, &sz, finfo);
        if (r)
            xefi_fatal("GetInfo", r);
        printf("FileSize %ld\n", finfo->FileSize);

        sz = sizeof(buf) - 1;
        r = file->Read(file, &sz, buf);
        if (r)
            xefi_fatal("Read", r);

        char* x = buf;
        while (sz-- > 0)
            printf("%c", *x++);

        file->Close(file);
    }

    root->Close(root);
    xefi_close_protocol(loaded->DeviceHandle, &SimpleFileSystemProtocol);
    xefi_close_protocol(img, &LoadedImageProtocol);

    xefi_wait_any_key();

    return EFI_SUCCESS;
}
