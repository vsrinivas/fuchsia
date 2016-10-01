// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>

#include <xefi.h>
#include <stdio.h>

void* xefi_load_file(char16_t* filename, size_t* _sz) {
    efi_loaded_image_protocol* loaded;
    efi_status r;
    void* data = NULL;
    size_t pages = 0;

    r = xefi_open_protocol(gImg, &LoadedImageProtocol, (void**)&loaded);
    if (r) {
        printf("LoadFile: Cannot open LoadedImageProtocol (%s)\n", xefi_strerror(r));
        goto exit0;
    }

#if 0
	printf("Img DeviceHandle='%s'\n", HandleToString(loaded->DeviceHandle));
	printf("Img FilePath='%s'\n", DevicePathToStr(loaded->FilePath));
	printf("Img Base=%lx Size=%lx\n", loaded->ImageBase, loaded->ImageSize);
#endif

    efi_simple_file_system_protocol* sfs;
    r = xefi_open_protocol(loaded->DeviceHandle, &SimpleFileSystemProtocol, (void**)&sfs);
    if (r) {
        printf("LoadFile: Cannot open SimpleFileSystemProtocol (%s)\n", xefi_strerror(r));
        goto exit1;
    }

    efi_file_protocol* root;
    r = sfs->OpenVolume(sfs, &root);
    if (r) {
        printf("LoadFile: Cannot open root volume (%s)\n", xefi_strerror(r));
        goto exit2;
    }

    efi_file_protocol* file;
    r = root->Open(root, &file, filename, EFI_FILE_MODE_READ, 0);
    if (r) {
        printf("LoadFile: Cannot open file (%s)\n", xefi_strerror(r));
        goto exit3;
    }

    char buf[512];
    size_t sz = sizeof(buf);
    efi_file_info* finfo = (void*)buf;
    r = file->GetInfo(file, &FileInfoGuid, &sz, finfo);
    if (r) {
        printf("LoadFile: Cannot get FileInfo (%s)\n", xefi_strerror(r));
        goto exit3;
    }

    pages = (finfo->FileSize + 4095) / 4096;
    r = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, (efi_physical_addr *)&data);
    if (r) {
        printf("LoadFile: Cannot allocate buffer (%s)\n", xefi_strerror(r));
        data = NULL;
        goto exit4;
    }

    sz = finfo->FileSize;
    r = file->Read(file, &sz, data);
    if (r) {
        printf("LoadFile: Error reading file (%s)\n", xefi_strerror(r));
        gBS->FreePages((efi_physical_addr)data, pages);
        data = NULL;
        goto exit4;
    }
    if (sz != finfo->FileSize) {
        printf("LoadFile: Short read\n");
        gBS->FreePages((efi_physical_addr)data, pages);
        data = NULL;
        goto exit4;
    }
    *_sz = finfo->FileSize;
exit4:
    file->Close(file);
exit3:
    root->Close(root);
exit2:
    xefi_close_protocol(loaded->DeviceHandle, &SimpleFileSystemProtocol);
exit1:
    xefi_close_protocol(gImg, &LoadedImageProtocol);
exit0:
    return data;
}
