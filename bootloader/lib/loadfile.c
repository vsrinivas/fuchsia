// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>

#include <xefi.h>
#include <stdio.h>

#ifndef VERBOSE
#define xprintf(...) do {} while (0);
#else
#define xprintf(fmt...) printf(fmt)
#endif

efi_file_protocol* xefi_open_file(char16_t* filename) {
    efi_loaded_image_protocol* loaded;
    efi_status r;
    efi_file_protocol* file = NULL;

    r = xefi_open_protocol(gImg, &LoadedImageProtocol, (void**)&loaded);
    if (r) {
        xprintf("LoadFile: Cannot open LoadedImageProtocol (%s)\n", xefi_strerror(r));
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
        xprintf("LoadFile: Cannot open SimpleFileSystemProtocol (%s)\n", xefi_strerror(r));
        goto exit1;
    }

    efi_file_protocol* root;
    r = sfs->OpenVolume(sfs, &root);
    if (r) {
        xprintf("LoadFile: Cannot open root volume (%s)\n", xefi_strerror(r));
        goto exit2;
    }

    r = root->Open(root, &file, filename, EFI_FILE_MODE_READ, 0);
    if (r) {
        xprintf("LoadFile: Cannot open file (%s)\n", xefi_strerror(r));
        goto exit3;
    }

exit3:
    root->Close(root);
exit2:
    xefi_close_protocol(loaded->DeviceHandle, &SimpleFileSystemProtocol);
exit1:
    xefi_close_protocol(gImg, &LoadedImageProtocol);
exit0:
    return file;
}

void* xefi_read_file(efi_file_protocol* file, size_t* _sz, size_t front_bytes) {
    efi_status r;
    size_t pages = 0;
    void* data = NULL;

    char buf[512];
    size_t sz = sizeof(buf);
    efi_file_info* finfo = (void*)buf;
    r = file->GetInfo(file, &FileInfoGuid, &sz, finfo);
    if (r) {
        xprintf("LoadFile: Cannot get FileInfo (%s)\n", xefi_strerror(r));
        return NULL;
    }

    pages = (finfo->FileSize + front_bytes + 4095) / 4096;
    r = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, (efi_physical_addr *)&data);
    if (r) {
        xprintf("LoadFile: Cannot allocate buffer (%s)\n", xefi_strerror(r));
        return NULL;
    }

    sz = finfo->FileSize;
    r = file->Read(file, &sz, data + front_bytes);
    if (r) {
        xprintf("LoadFile: Error reading file (%s)\n", xefi_strerror(r));
        gBS->FreePages((efi_physical_addr)data, pages);
        return NULL;
    }
    if (sz != finfo->FileSize) {
        xprintf("LoadFile: Short read\n");
        gBS->FreePages((efi_physical_addr)data, pages);
        return NULL;
    }
    *_sz = finfo->FileSize;

    return data + front_bytes;
}

void* xefi_load_file(char16_t* filename, size_t* _sz, size_t front_bytes) {
    efi_file_protocol* file = xefi_open_file(filename);
    if (!file) {
        return NULL;
    }
    void* data = xefi_read_file(file, _sz, front_bytes);
    file->Close(file);
    return data;
}
