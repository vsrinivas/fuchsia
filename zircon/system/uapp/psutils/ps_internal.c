// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ps_internal.h"

#include <stdlib.h>
#include <string.h>

#include <pretty/sizes.h>

void print_header(int id_w, const ps_options_t* options, FILE* out) {
  if (options->also_show_threads) {
    fprintf(out, "%*s %7s %7s %7s %7s %s\n", -id_w, "TASK", "PSS", "PRIVATE", "SHARED", "STATE",
            "NAME");
  } else if (options->only_show_jobs) {
    fprintf(out, "%*s %7s %7s %7s %s\n", -id_w, "TASK", "PSS", "PRIVATE", "STATE", "NAME");
  } else {
    fprintf(out, "%*s %7s %7s %7s %7s %s\n", -id_w, "TASK", "PSS", "PRIVATE", "SHARED", "STATE",
            "NAME");
  }
}

void print_table(task_table_t* table, const ps_options_t* options, FILE* out) {
  if (table->num_entries == 0) {
    return;
  }

  // Find the width of the id column; the rest are fixed or don't matter.
  int id_w = 0;
  for (size_t i = 0; i < table->num_entries; i++) {
    const task_entry_t* e = table->entries + i;
    // Indentation + type + : + space + koid
    int w = 2 * e->depth + 3 + strlen(e->koid_str);
    if (w > id_w) {
      id_w = w;
    }
  }

  print_header(id_w, options, out);
  char* idbuf = (char*)malloc(id_w + 1);
  for (size_t i = 0; i < table->num_entries; i++) {
    const task_entry_t* e = table->entries + i;
    if (e->type == 't' && !options->also_show_threads) {
      continue;
    }
    snprintf(idbuf, id_w + 1, "%*s%c: %s", e->depth * 2, "", e->type, e->koid_str);

    // Format the size fields for entry types that need them.
    char pss_bytes_str[MAX_FORMAT_SIZE_LEN] = {};
    char private_bytes_str[MAX_FORMAT_SIZE_LEN] = {};
    if (e->type == 'j' || e->type == 'p') {
      format_size_fixed(pss_bytes_str, sizeof(pss_bytes_str), e->pss_bytes, options->format_unit);
      format_size_fixed(private_bytes_str, sizeof(private_bytes_str), e->private_bytes,
                        options->format_unit);
    }
    char shared_bytes_str[MAX_FORMAT_SIZE_LEN] = {};
    if (e->type == 'p') {
      format_size_fixed(shared_bytes_str, sizeof(shared_bytes_str), e->shared_bytes,
                        options->format_unit);
    }

    if (options->also_show_threads) {
      fprintf(out, "%*s %7s %7s %7s %7s %s\n", -id_w, idbuf, pss_bytes_str, private_bytes_str,
              shared_bytes_str, e->state_str, e->name);
    } else if (options->only_show_jobs) {
      fprintf(out, "%*s %7s %7s %7s %s\n", -id_w, idbuf, pss_bytes_str, private_bytes_str,
              e->state_str, e->name);
    } else {
      fprintf(out, "%*s %7s %7s %7s %7s %s\n", -id_w, idbuf, pss_bytes_str, private_bytes_str,
              shared_bytes_str, e->state_str, e->name);
    }
  }
  free(idbuf);
  print_header(id_w, options, out);
}

