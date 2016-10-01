// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi/system-table.h>
#include <efi/types.h>
#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>

#include <stdio.h>
#include <utils.h>

EFIAPI efi_status efi_main(efi_handle img, efi_system_table* sys) {
    efi_loaded_image_protocol* loaded;
    efi_status r;

    InitGoodies(img, sys);

    printf("Hello, EFI World\n");

    r = OpenProtocol(img, &LoadedImageProtocol, (void**)&loaded);
    if (r)
        Fatal("LoadedImageProtocol", r);

    printf("Img DeviceHandle='%ls'\n", HandleToString(loaded->DeviceHandle));
    printf("Img FilePath='%ls'\n", DevicePathToStr(loaded->FilePath));
    printf("Img Base=%p Size=%lx\n", loaded->ImageBase, loaded->ImageSize);

    efi_simple_file_system_protocol* sfs;
    r = OpenProtocol(loaded->DeviceHandle, &SimpleFileSystemProtocol, (void**)&sfs);
    if (r)
        Fatal("SimpleFileSystemProtocol", r);

    efi_file_protocol* root;
    r = sfs->OpenVolume(sfs, &root);
    if (r)
        Fatal("OpenVolume", r);

    efi_file_protocol* file;
    r = root->Open(root, &file, L"README.txt", EFI_FILE_MODE_READ, 0);

    if (r == EFI_SUCCESS) {
        char buf[512];
        size_t sz = sizeof(buf);
        efi_file_info* finfo = (void*)buf;
        r = file->GetInfo(file, &FileInfoGuid, &sz, finfo);
        if (r)
            Fatal("GetInfo", r);
        printf("FileSize %ld\n", finfo->FileSize);

        sz = sizeof(buf) - 1;
        r = file->Read(file, &sz, buf);
        if (r)
            Fatal("Read", r);

        char* x = buf;
        while (sz-- > 0)
            printf("%c", *x++);

        file->Close(file);
    }

    root->Close(root);
    CloseProtocol(loaded->DeviceHandle, &SimpleFileSystemProtocol);
    CloseProtocol(img, &LoadedImageProtocol);

    WaitAnyKey();

    return EFI_SUCCESS;
}
