// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO: Taken from crashlogger. Revisit.

#include <fcntl.h>
#include <link.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "dso_list.h"
#include "util.h"

namespace debugserver {

const char kDebugDirectory[] = "/boot/debug";
const char kDebugSuffix[] = ".debug";

static dsoinfo_t* dsolist_add(dsoinfo_t** list, const char* name,
                              uintptr_t base) {
  if (!strcmp(name, "libc.so")) {
    name = "libmusl.so";
  }
  size_t len = strlen(name);
  size_t alloc_bytes = sizeof(dsoinfo_t) + len + 1;
  auto dso = reinterpret_cast<dsoinfo_t*>(calloc(1, alloc_bytes));
  if (dso == nullptr) {
    FXL_LOG(FATAL) << "OOM allocating " << alloc_bytes << " bytes";
    exit(1);
  }
  memcpy(dso->name, name, len + 1);
  memset(dso->buildid, 'x', sizeof(dso->buildid) - 1);
  dso->base = base;
  dso->debug_file_tried = false;
  dso->debug_file_status = ZX_ERR_BAD_STATE;
  while (*list != nullptr) {
    if ((*list)->base < dso->base) {
      dso->next = *list;
      *list = dso;
      return dso;
    }
    list = &((*list)->next);
  }
  *list = dso;
  dso->next = nullptr;
  return dso;
}

dsoinfo_t* dso_fetch_list(std::shared_ptr<ByteBlock> bb, zx_vaddr_t lmap_addr,
                          const char* name) {
  dsoinfo_t* dsolist = nullptr;
  // The first dso we see is the main executable.
  bool is_main_exec = true;

  while (lmap_addr != 0) {
    char dsoname[64];
    struct link_map lmap;

    // If there's a failure here, say because the internal data structures got
    // corrupted, just bail and return what we've collected so far.

    if (!bb->Read(lmap_addr, &lmap, sizeof(lmap))) break;
    if (!ReadString(*bb, reinterpret_cast<zx_vaddr_t>(lmap.l_name), dsoname,
                    sizeof(dsoname)))
      break;

    const char* file_name = dsoname[0] ? dsoname : name;
    dsoinfo_t* dso = dsolist_add(&dsolist, file_name, lmap.l_addr);

    std::unique_ptr<ElfReader> elf_reader;
    ElfError rc =
        ElfReader::Create(file_name, bb, 0, dso->base, &elf_reader);
    if (rc != ElfError::OK) {
      FXL_LOG(ERROR) << "Unable to read ELF file: " << ElfErrorName(rc);
      break;
    }

    auto hdr = elf_reader->header();
    rc = elf_reader->ReadSegmentHeaders();
    if (rc != ElfError::OK) {
      FXL_LOG(ERROR) << "Error reading ELF segment headers: "
                     << ElfErrorName(rc);
    } else {
      size_t num_segments = elf_reader->GetNumSegments();
      uint32_t num_loadable_phdrs = 0;
      for (size_t i = 0; i < num_segments; ++i) {
        const ElfSegmentHeader& phdr = elf_reader->GetSegmentHeader(i);
        if (phdr.p_type == PT_LOAD) ++num_loadable_phdrs;
      }
      // malloc may, or may not, return NULL for a zero byte request.
      // Remove the ambiguity for consumers and always use NULL if there no
      // loadable phdrs.
      ElfSegmentHeader* loadable_phdrs = NULL;
      if (num_loadable_phdrs > 0) {
        loadable_phdrs = reinterpret_cast<ElfSegmentHeader*>(
            malloc(num_loadable_phdrs * hdr.e_phentsize));
      }
      if (loadable_phdrs || num_loadable_phdrs == 0) {
        size_t j = 0;
        for (size_t i = 0; i < num_segments; ++i) {
          const ElfSegmentHeader& phdr = elf_reader->GetSegmentHeader(i);
          if (phdr.p_type == PT_LOAD) loadable_phdrs[j++] = phdr;
        }
        FXL_DCHECK(j == num_loadable_phdrs);
        dso->num_loadable_phdrs = num_loadable_phdrs;
        dso->loadable_phdrs = loadable_phdrs;
      } else {
        FXL_LOG(ERROR) << "OOM reading phdrs";
      }
    }

    rc = elf_reader->ReadBuildId(dso->buildid, sizeof(dso->buildid));
    if (rc != ElfError::OK) {
      // This isn't fatal so don't flag as an error.
      FXL_VLOG(1) << "Unable to read build id: " << ElfErrorName(rc);
    }

    dso->is_main_exec = is_main_exec;
    dso->entry = dso->base + hdr.e_entry;
    dso->phdr = dso->base + hdr.e_phoff;
    dso->phentsize = hdr.e_phentsize;
    dso->phnum = hdr.e_phnum;

    is_main_exec = false;
    lmap_addr = reinterpret_cast<zx_vaddr_t>(lmap.l_next);
  }

  return dsolist;
}

void dso_free_list(dsoinfo_t* list) {
  while (list != nullptr) {
    dsoinfo_t* next = list->next;
    free(list->loadable_phdrs);
    free(list->debug_file);
    free(list);
    list = next;
  }
}

dsoinfo_t* dso_lookup(dsoinfo_t* dso_list, zx_vaddr_t pc) {
  for (auto dso = dso_list; dso != nullptr; dso = dso->next) {
    if (pc >= dso->base) return dso;
  }

  return nullptr;
}

dsoinfo_t* dso_get_main_exec(dsoinfo_t* dso_list) {
  for (auto dso = dso_list; dso != nullptr; dso = dso->next) {
    if (dso->is_main_exec) return dso;
  }

  return nullptr;
}

void dso_print_list(FILE* out, const dsoinfo_t* dso_list) {
  for (auto dso = dso_list; dso != nullptr; dso = dso->next) {
    fprintf(out, "dso: id=%s base=%p name=%s\n", dso->buildid, (void*)dso->base,
            dso->name);
  }
}

void dso_vlog_list(const dsoinfo_t* dso_list) {
  for (auto dso = dso_list; dso != nullptr; dso = dso->next) {
    FXL_VLOG(2) << fxl::StringPrintf("dso: id=%s base=%p name=%s", dso->buildid,
                                     (void*)dso->base, dso->name);
  }
}

zx_status_t dso_find_debug_file(dsoinfo_t* dso, const char** out_debug_file) {
  // Have we already tried?
  // Yeah, if we OOM it's possible it'll succeed next time, but
  // it's not worth the extra complexity to avoid printing the debugging
  // messages twice.
  if (dso->debug_file_tried) {
    switch (dso->debug_file_status) {
      case ZX_OK:
        FXL_DCHECK(dso->debug_file != nullptr);
        *out_debug_file = dso->debug_file;
      // fall through
      default:
        FXL_VLOG(2) << "returning " << dso->debug_file_status
                    << ", already tried to find debug file for " << dso->name;
        return dso->debug_file_status;
    }
  }

  dso->debug_file_tried = true;

  char* path;
  if (asprintf(&path, "%s/%s%s", kDebugDirectory, dso->buildid, kDebugSuffix) <
      0) {
    FXL_VLOG(1) << "OOM building debug file path for dso " << dso->name;
    dso->debug_file_status = ZX_ERR_NO_MEMORY;
    return dso->debug_file_status;
  }

  FXL_VLOG(1) << "looking for debug file " << path;

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    FXL_VLOG(1) << "debug file for dso " << dso->name << " not found: " << path;
    free(path);
    dso->debug_file_status = ZX_ERR_NOT_FOUND;
  } else {
    FXL_VLOG(1) << "found debug file for dso " << dso->name << ": " << path;
    close(fd);
    dso->debug_file = path;
    *out_debug_file = path;
    dso->debug_file_status = ZX_OK;
  }

  return dso->debug_file_status;
}

}  // namespace debugserver
