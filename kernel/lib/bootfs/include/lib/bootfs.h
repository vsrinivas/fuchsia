// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <compiler.h>
#include <inttypes.h>
#include <lib/bio.h>
#include <list.h>

__BEGIN_CDECLS

#define BOOTFS_MAX_NAME_LEN 256

struct bootfs_file {
    struct list_node node;

    uint64_t offset;
    uint64_t len;
    bdev_t* bdev;

    char name[0];
};

/* parse a bootfs archive off of a block device */
/* returns the number of files added to the list, or error */
int bootfs_parse_bio(bdev_t* bdev, uint64_t offset, struct list_node* list) __NONNULL((1)) __NONNULL((3));

__END_CDECLS
