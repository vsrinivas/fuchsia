// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "driver.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/driver/binding.h>
#include <zircon/status.h>

#include <new>
#include <string>

#include <driver-info/driver-info.h>
#include <fbl/string_printf.h>

#include "env.h"
#include "fdio.h"
#include "src/devices/lib/log/log.h"

namespace {

struct AddContext {
  const char* libname;
  DriverLoadCallback func;
};

bool is_driver_disabled(const char* name) {
  // driver.<driver_name>.disable
  auto option = fbl::StringPrintf("driver.%s.disable", name);
  return getenv_bool(option.data(), false);
}

void found_driver(zircon_driver_note_payload_t* note, const zx_bind_inst_t* bi, void* cookie) {
  auto context = static_cast<const AddContext*>(cookie);

  // ensure strings are terminated
  note->name[sizeof(note->name) - 1] = 0;
  note->vendor[sizeof(note->vendor) - 1] = 0;
  note->version[sizeof(note->version) - 1] = 0;

  if (is_driver_disabled(note->name)) {
    return;
  }

  auto drv = std::make_unique<Driver>();
  if (drv == nullptr) {
    return;
  }

  auto binding = std::make_unique<zx_bind_inst_t[]>(note->bindcount);
  if (binding == nullptr) {
    return;
  }
  const size_t bindlen = note->bindcount * sizeof(zx_bind_inst_t);
  memcpy(binding.get(), bi, bindlen);
  drv->binding.reset(binding.release());
  drv->binding_size = static_cast<uint32_t>(bindlen);

  drv->flags = note->flags;
  drv->libname.Set(context->libname);
  drv->name.Set(note->name);

  VLOGF(2, "Found driver: %s", (char*)cookie);
  VLOGF(2, "        name: %s", note->name);
  VLOGF(2, "      vendor: %s", note->vendor);
  VLOGF(2, "     version: %s", note->version);
  VLOGF(2, "       flags: %#x", note->flags);
  VLOGF(2, "     binding:");
  for (size_t n = 0; n < note->bindcount; n++) {
    VLOGF(2, "         %03zd: %08x %08x", n, bi[n].op, bi[n].arg);
  }

  context->func(drv.release(), note->version);
}

}  // namespace

void find_loadable_drivers(const std::string& path, DriverLoadCallback func) {
  DIR* dir = opendir(path.c_str());
  if (dir == nullptr) {
    return;
  }
  AddContext context = {"", std::move(func)};

  struct dirent* de;
  while ((de = readdir(dir)) != nullptr) {
    if (de->d_name[0] == '.') {
      continue;
    }
    if (de->d_type != DT_REG) {
      continue;
    }
    auto libname = fbl::StringPrintf("%s/%s", path.c_str(), de->d_name);
    context.libname = libname.data();

    int fd = openat(dirfd(dir), de->d_name, O_RDONLY);
    if (fd < 0) {
      continue;
    }
    zx_status_t status = di_read_driver_info(fd, &context, found_driver);
    close(fd);

    if (status == ZX_ERR_NOT_FOUND) {
      LOGF(INFO, "Missing info from driver '%s'", libname.data());
    } else if (status != ZX_OK) {
      LOGF(ERROR, "Failed to read info from driver '%s': %s", libname.data(),
           zx_status_get_string(status));
    }
  }
  closedir(dir);
}

void load_driver(const char* path, DriverLoadCallback func) {
  // TODO: check for duplicate driver add
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    LOGF(ERROR, "Cannot open driver '%s'", path);
    return;
  }

  AddContext context = {path, std::move(func)};
  zx_status_t status = di_read_driver_info(fd, &context, found_driver);
  close(fd);

  if (status == ZX_ERR_NOT_FOUND) {
    LOGF(INFO, "Missing info from driver '%s'", path);
  } else if (status != ZX_OK) {
    LOGF(ERROR, "Failed to read info from driver '%s': %s", path, zx_status_get_string(status));
  }
}
