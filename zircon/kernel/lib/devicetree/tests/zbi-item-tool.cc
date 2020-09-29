// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>
#include <zircon/status.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "zbi.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s FILE.dtb\n", argv[0]);
    return 2;
  }

  if (auto item = DevicetreeItem::Get(); item.is_error()) {
    fprintf(stderr, "Cannot get devicetree ZBI item: %s\n",
            zx_status_get_string(item.error_value()));
    return 1;
  } else {
    FILE* f = fopen(argv[1], "w");
    if (!f) {
      fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
      return 1;
    }
    auto cleanup = fit::defer([f]() { fclose(f); });

    uint32_t offset = 0;
    while (offset < item->size) {
      char buffer[BUFSIZ];
      const size_t read_size = std::min(item->size, static_cast<uint32_t>(sizeof(buffer)));
      if (zx_status_t status = item->vmo.read(buffer, offset, read_size); status != ZX_OK) {
        fprintf(stderr, "zx_vmo_read: %s\n", zx_status_get_string(status));
        return 1;
      }

      fwrite(buffer, read_size, 1, f);
      if (ferror(f)) {
        fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
        return 1;
      }

      offset += read_size;
    }

    return 0;
  }
}
