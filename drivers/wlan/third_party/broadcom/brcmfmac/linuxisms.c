/*
 * Copyright 2018 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


#include "linuxisms.h"

uint64_t jiffies; // To make it link, jiffies has to be defined (not just declared)
struct current_with_pid* current; // likewise current


// TODO(jeffbrown): Once we have an equivalent of debugfs, implement / connect these.
zx_status_t debugfs_create_dir(char *name, struct dentry* parent,
                               struct dentry** new_folder_out) {
    if (new_folder_out) {
        *new_folder_out = NULL;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t debugfs_create_devm_seqfile(void* dev, const char* fn, struct dentry* parent,
                                        zx_status_t (*read_fn)(struct seq_file* seq,
                                                               void* data),
                                        struct dentry** new_file_out) {
    if (new_file_out) {
        *new_file_out = NULL;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t debugfs_remove_recursive(struct dentry* dir) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t debugfs_create_u32(const char* name, uint32_t permissions, struct dentry* dentry,
                               uint32_t* console_interval_out) {
    return ZX_ERR_NOT_SUPPORTED;
}

