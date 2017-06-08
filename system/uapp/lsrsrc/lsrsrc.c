// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <magenta/device/sysinfo.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/resource.h>

void dump_resource(mx_handle_t h, mx_rrec_self_t* self, unsigned indent) {
    for (unsigned n = 0; n < indent; n++) {
        printf("  ");
    }
    printf("[%s] t=%04x r=%u c=%u\n",
           self->name, self->subtype,
           self->record_count, self->child_count);

    if (self->child_count == 0) {
        return;
    }

    mx_rrec_t* list;
    size_t sz = sizeof(mx_rrec_t) * self->child_count * 2;
    if ((list = malloc(sz)) == NULL) {
        fprintf(stderr, "lsrsrc: out of memory\n");
        return;
    }

    mx_status_t r;
    size_t count;
    if ((r = mx_object_get_info(h, MX_INFO_RESOURCE_CHILDREN, list, sz, &count, NULL)) < 0) {
        fprintf(stderr, "lsrsrc: cannot get children\n");
        free(list);
        return;
    }

    for (unsigned n = 0; n < count; n++) {
        mx_handle_t ch;
        if ((r = mx_object_get_child(h, list[n].self.koid, MX_RIGHT_SAME_RIGHTS, &ch)) < 0) {
            fprintf(stderr, "lsrsrc: cannot get child handle\n");
            break;
        }
        dump_resource(ch, &list[n].self, indent + 1);
        mx_handle_close(ch);
    }
    free(list);
};

void dump_resource_tree(mx_handle_t h, unsigned indent) {
    mx_rrec_t rrec;

    mx_status_t r;
    size_t count;
    if ((r = mx_object_get_info(h, MX_INFO_RESOURCE_RECORDS, &rrec, sizeof(rrec), &count, 0)) < 0) {
        fprintf(stderr, "lsrsrc: cannot get records: %d\n", r);
        return;
    }
    if ((count < 1) || (rrec.type != MX_RREC_SELF)) {
        return;
    }
    dump_resource(h, &rrec.self, indent);
}

int main(int argc, char** argv) {
    int fd;

    if ((fd = open("/dev/misc/sysinfo", O_RDONLY)) < 0) {
        fprintf(stderr, "lsrsrc: cannot open sysinfo\n");
        return -1;
    }

    mx_handle_t h = 0;
    mx_status_t r;
    if ((r = ioctl_sysinfo_get_root_resource(fd, &h)) != sizeof(h)) {
        fprintf(stderr, "h = %x\n", h);
        fprintf(stderr, "lsrsrc: cannot obtain root resource: %d\n", r);
        return -1;
    }

    close(fd);
    dump_resource_tree(h, 0);
    return 0;
}
