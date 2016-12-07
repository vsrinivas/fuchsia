// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO: Taken from crashlogger. Revisit.

#include <fcntl.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/types.h>
#include <magenta/syscalls.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

#include "dso-list.h"
#include "util.h"

extern struct r_debug* _dl_debug_addr;

namespace debugserver {
namespace elf {

// TODO: For now we make the simplifying assumption that the address of
// this variable in our address space is constant among all processes.
#define rdebug_vaddr ((uintptr_t)_dl_debug_addr)
#define rdebug_off_lmap offsetof(struct r_debug, r_map)

#define lmap_off_next offsetof(struct link_map, l_next)
#define lmap_off_name offsetof(struct link_map, l_name)
#define lmap_off_addr offsetof(struct link_map, l_addr)

const char kDebugDirectory[] = "/boot/debug";
const char kDebugSuffix[] = ".debug";

static dsoinfo_t* dsolist_add(dsoinfo_t** list,
                              const char* name,
                              uintptr_t base) {
  if (!strcmp(name, "libc.so")) {
    name = "libmusl.so";
  }
  size_t len = strlen(name);
  size_t alloc_bytes = sizeof(dsoinfo_t) + len + 1;
  auto dso = reinterpret_cast<dsoinfo_t*>(calloc(1, alloc_bytes));
  if (dso == nullptr) {
    FTL_LOG(FATAL) << "OOM allocating " << alloc_bytes << " bytes";
    exit(1);
  }
  memcpy(dso->name, name, len + 1);
  memset(dso->buildid, 'x', sizeof(dso->buildid) - 1);
  dso->base = base;
  dso->debug_file_tried = false;
  dso->debug_file_status = ERR_BAD_STATE;
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

dsoinfo_t* dso_fetch_list(const util::Memory& m,
                          mx_vaddr_t lmap_addr,
                          const char* name) {
  dsoinfo_t* dsolist = nullptr;
  // The first dso we see is the main executable.
  bool is_main_exec = true;

  while (lmap_addr != 0) {
    char dsoname[64];
    struct link_map lmap;

    // If there's a failure here, say because the internal data structures got
    // corrupted, just bail and return what we've collected so far.

    if (!m.Read(lmap_addr, &lmap, sizeof(lmap)))
      break;
    if (!util::ReadString(m, reinterpret_cast<mx_vaddr_t>(lmap.l_name), dsoname,
                          sizeof(dsoname)))
      break;

    dsoinfo_t* dso =
        dsolist_add(&dsolist, dsoname[0] ? dsoname : name, lmap.l_addr);
    ehdr_type ehdr;
    if (!ReadElfHdr(m, dso->base, &ehdr))
      break;
    if (!VerifyElfHdr(&ehdr))
      break;

    // Ignore failures, this isn't critical.
    ReadBuildId(m, dso->base, &ehdr, dso->buildid, sizeof(dso->buildid));
    dso->is_main_exec = is_main_exec;
    dso->entry = dso->base + ehdr.e_entry;
    dso->phdr = dso->base + ehdr.e_phoff;
    dso->phentsize = ehdr.e_phentsize;
    dso->phnum = ehdr.e_phnum;

    is_main_exec = false;
    lmap_addr = reinterpret_cast<mx_vaddr_t>(lmap.l_next);
  }

  return dsolist;
}

void dso_free_list(dsoinfo_t* list) {
  while (list != NULL) {
    dsoinfo_t* next = list->next;
    free(list->debug_file);
    free(list);
    list = next;
  }
}

dsoinfo_t* dso_lookup(dsoinfo_t* dso_list, mx_vaddr_t pc) {
  for (auto dso = dso_list; dso != NULL; dso = dso->next) {
    if (pc >= dso->base)
      return dso;
  }

  return nullptr;
}

dsoinfo_t* dso_get_main_exec(dsoinfo_t* dso_list) {
  for (auto dso = dso_list; dso != NULL; dso = dso->next) {
    if (dso->is_main_exec)
      return dso;
  }

  return nullptr;
}

void dso_vlog_list(dsoinfo_t* dso_list) {
  for (auto dso = dso_list; dso != nullptr; dso = dso->next) {
    FTL_VLOG(2) << ftl::StringPrintf("dso: id=%s base=%p name=%s", dso->buildid,
                                     (void*)dso->base, dso->name);
  }
}

mx_status_t dso_find_debug_file(dsoinfo_t* dso, const char** out_debug_file) {
  // Have we already tried?
  // Yeah, if we OOM it's possible it'll succeed next time, but
  // it's not worth the extra complexity to avoid printing the debugging
  // messages twice.
  if (dso->debug_file_tried) {
    switch (dso->debug_file_status) {
      case NO_ERROR:
        FTL_DCHECK(dso->debug_file != nullptr);
        *out_debug_file = dso->debug_file;
      // fall through
      default:
        FTL_VLOG(2) << "returning " << dso->debug_file_status
                    << ", already tried to find debug file for " << dso->name;
        return dso->debug_file_status;
    }
  }

  dso->debug_file_tried = true;

  char* path;
  if (asprintf(&path, "%s/%s%s", kDebugDirectory, dso->buildid, kDebugSuffix) <
      0) {
    FTL_VLOG(1) << "OOM building debug file path for dso " << dso->name;
    dso->debug_file_status = ERR_NO_MEMORY;
    return dso->debug_file_status;
  }

  FTL_VLOG(1) << "looking for debug file " << path;

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    FTL_VLOG(1) << "debug file for dso " << dso->name << " not found: " << path;
    free(path);
    dso->debug_file_status = ERR_NOT_FOUND;
  } else {
    FTL_VLOG(1) << "found debug file for dso " << dso->name << ": " << path;
    close(fd);
    dso->debug_file = path;
    *out_debug_file = path;
    dso->debug_file_status = NO_ERROR;
  }

  return dso->debug_file_status;
}

}  // namespace elf
}  // namespace debugserver
