// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* mkkdtb - packages kernel file and dtb in kdtb format, required by some
            bootloaders instead of just appending dtb to end of kernel.
*/

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    char magic[4];
    uint32_t kern_size;
    uint32_t dtb_size;
} kdtb_header_t;



int print_usage(void) {
    printf("mkkdtb kernfile dtbfile outfile\n");
    return 0;
}


int main(int argc, char** argv) {

    int retval = 0;

    uint8_t *kern_buffer = NULL;
    uint8_t *dtb_buffer = NULL;

    FILE *dtb_fd = NULL;
    FILE *out_fd = NULL;

    kdtb_header_t kheader = {"KDTB", 0, 0};

    if (argc < 4) {
        print_usage();
        return -1;
    }

    FILE *kernel_fd = fopen(argv[1], "r");
    if (!kernel_fd) {
        fprintf(stderr, "Kernel file not found\n");
        retval = -1;
        goto fail;
    }

    dtb_fd = fopen(argv[2], "r");
    if (!dtb_fd) {
        fprintf(stderr, "DTB file not found\n");
        retval = -1;
        goto fail;
    }

    out_fd = fopen(argv[3], "w");
    if (!out_fd) {
        fprintf(stderr, "Can't open output file\n");
        retval=-1;
        goto fail;
    }

    fseek(kernel_fd, 0, SEEK_END);
    kheader.kern_size = ftell(kernel_fd);
    fseek(kernel_fd, 0, SEEK_SET);

    fseek(dtb_fd, 0, SEEK_END);
    kheader.dtb_size = ftell(dtb_fd);
    fseek(dtb_fd, 0, SEEK_SET);

    kern_buffer = malloc(kheader.kern_size);
    if (!kern_buffer) {
        retval = -1;
        goto fail;
    }
    if (fread(kern_buffer, kheader.kern_size, 1, kernel_fd) != 1) {
        fprintf(stderr, "kernel read error\n");
        retval = -1;
        goto fail;
    }

    dtb_buffer = malloc(kheader.dtb_size);
    if (!dtb_buffer) {
        retval = -1;
        goto fail;
    }
    if (fread(dtb_buffer,kheader.dtb_size, 1, dtb_fd) != 1) {
        fprintf(stderr, "dtb read error\n");
        retval = -1;
        goto fail;
    }

    fwrite(&kheader, sizeof(kheader), 1, out_fd);
    fwrite(kern_buffer, sizeof(uint8_t), kheader.kern_size, out_fd);
    fwrite(dtb_buffer, sizeof(uint8_t), kheader.dtb_size, out_fd);

fail:
    if (kernel_fd) fclose(kernel_fd);
    if (dtb_fd) fclose(dtb_fd);
    if (out_fd) fclose(out_fd);
    if (kern_buffer) free(kern_buffer);
    if (dtb_buffer) free(dtb_buffer);
    return retval;
}