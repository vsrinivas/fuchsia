// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "handles-internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include <task-utils/get.h>

#include "object-utils.h"

namespace {

bool skip_handle(zx_obj_type_t type, Filter filter) {
  if (filter == kAll) {
    return false;
  }

  uint64_t mask = 1u << (type - 1);
  return ((filter & mask) == 0u);
}

void calc_num_digits(const std::vector<zx_info_handle_extended_t>& handles, Filter filter,
                     int* digits_koid, int* digits_related_koid) {
  // To format nicely we need to find out sizes of printed koids, which can vary greatly since
  // they are 64 bits, but start in the small range (5 digits) and grow slowly.
  int d_koid = 0;
  int d_rkoid = 0;
  for (const auto& info : handles) {
    if (skip_handle(info.type, filter)) {
      continue;
    }
    d_koid = std::max(d_koid, static_cast<int>(std::log10(info.koid)) + 1);
    if (info.related_koid) {
      d_rkoid = std::max(d_rkoid, static_cast<int>(std::log10(info.related_koid)) + 1);
    }
  }

  *digits_koid = d_koid;
  *digits_related_koid = d_rkoid;
}

}  // namespace

Filter operator+=(Filter& lhs, const Filter& rhs) {
  lhs = static_cast<Filter>(lhs + rhs);
  return lhs;
}

Filter operator~(const Filter& rhs) {
  auto res = static_cast<Filter>(~static_cast<uint64_t>(rhs));
  return res;
}

size_t print_handles(FILE* f, const std::vector<zx_info_handle_extended_t>& handles,
                     Filter filter) {
  if (handles.empty()) {
    return 0;
  }

  // The number of digits is used to align the output in columns.
  int num_digits_koid = 0;
  int num_digits_rkoid = 0;
  calc_num_digits(handles, filter, &num_digits_koid, &num_digits_rkoid);

  size_t shown_handles = 0;
  for (const auto& info : handles) {
    if (skip_handle(info.type, filter)) {
      continue;
    }

    if (shown_handles == 0) {
      // First row about to show, print header first.
      fprintf(f, "%10s  %*s %*s %10s %s\n", "handle", num_digits_koid, "koid", num_digits_rkoid,
              num_digits_rkoid ? "rkoid" : " ", "rights", "type");
    }

    if (info.related_koid) {
      fprintf(f, "0x%08x: %*zu %*zu 0x%08x %s\n", info.handle_value, num_digits_koid, info.koid,
              num_digits_rkoid, info.related_koid, info.rights, obj_type_get_name(info.type));
    } else {
      fprintf(f, "0x%08x: %*zu %*c 0x%08x %s\n", info.handle_value, num_digits_koid, info.koid,
              num_digits_rkoid, ' ', info.rights, obj_type_get_name(info.type));
    }

    ++shown_handles;
  }

  fprintf(f, "%zu handles\n", shown_handles);
  return shown_handles;
}
