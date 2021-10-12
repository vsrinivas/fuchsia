// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <log.h>
#include <stdio.h>
#include <xefi.h>

#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>

efi_file_protocol* xefi_open_file(const char16_t* filename) {
  efi_loaded_image_protocol* loaded;
  efi_status r;
  efi_file_protocol* file = NULL;

  r = xefi_open_protocol(gImg, &LoadedImageProtocol, (void**)&loaded);
  if (r) {
    ELOG_S(r, "LoadFile: Cannot open LoadedImageProtocol");
    goto exit0;
  }

  efi_simple_file_system_protocol* sfs;
  r = xefi_open_protocol(loaded->DeviceHandle, &SimpleFileSystemProtocol, (void**)&sfs);
  if (r) {
    ELOG_S(r, "LoadFile: Cannot open SimpleFileSystemProtocol");
    goto exit1;
  }

  efi_file_protocol* root;
  r = sfs->OpenVolume(sfs, &root);
  if (r) {
    ELOG_S(r, "LoadFile: Cannot open root volume");
    goto exit2;
  }

  r = root->Open(root, &file, filename, EFI_FILE_MODE_READ, 0);
  if (r) {
    ELOG_S(r, "LoadFile: Cannot open file");
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
    ELOG_S(r, "LoadFile: Cannot get FileInfo");
    return NULL;
  }

  pages = (finfo->FileSize + front_bytes + 4095) / 4096;
  r = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, (efi_physical_addr*)&data);
  if (r) {
    ELOG_S(r, "LoadFile: Cannot allocate buffer");
    return NULL;
  }

  sz = finfo->FileSize;
  r = file->Read(file, &sz, data + front_bytes);
  if (r) {
    ELOG_S(r, "LoadFile: Error reading file");
    gBS->FreePages((efi_physical_addr)data, pages);
    return NULL;
  }
  if (sz != finfo->FileSize) {
    ELOG("LoadFile: Short read");
    gBS->FreePages((efi_physical_addr)data, pages);
    return NULL;
  }
  *_sz = finfo->FileSize;

  return data + front_bytes;
}

void* xefi_load_file(const char16_t* filename, size_t* _sz, size_t front_bytes) {
  efi_file_protocol* file = xefi_open_file(filename);
  if (!file) {
    return NULL;
  }
  void* data = xefi_read_file(file, _sz, front_bytes);
  file->Close(file);
  return data;
}
