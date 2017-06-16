// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


#include <mxio/io.h>

// change this number to change how many bytes are being written/read
#define TEST_LEN 1024

int write_read_pattern_test(int fd, const char* pattern, uint32_t length, uint32_t offset) {
    char in[length];
    uint8_t pattern_len = strlen(pattern);
    lseek(fd, offset, SEEK_SET);


    printf("Copying pattern %s, across %i bytes at offset %i", pattern, length, offset);
    // repeat this pattern across the buffer
    for (uint32_t i = 0; i + pattern_len < length; i += pattern_len) {
        memcpy(in + i, pattern, pattern_len);
    }
    memcpy(in + (length - (length % pattern_len)), pattern,
                 length % pattern_len);
    // uncomment this to print input buffer
    // for (int i = 0; i < TEST_LEN; i++) {
    //     printf("%02x ", ((unsigned char*)in)[i]);
    //     if (i % 50 == 49) {
    //         printf("\n");
    //     }
    // }
    printf("\n");
    int w = write(fd, &in, length);
    printf("Write completed. Bytes written: 0x%02x\n", w);

    // seek back to start
    lseek(fd, offset, SEEK_SET);
    char out[length];
    int r = read(fd, out, length);
    printf("Read completed. Bytes read: 0x%02x\n", r);
    // uncomment this to print output buffer
    // for (int i = 0; i < TEST_LEN; i++) {
    //     printf("%02x ", ((unsigned char*)out)[i]);
    //     if (i % 50 == 49) {
    //         printf("\n");
    //     }
    // }
    // printf("\n");

    printf("memcmp result: %i\n", memcmp(in, out, length));

    return memcmp(in, out, length);
}

// writes first block two blocks full of lowercase letters in order, then reads to verify.
// then writes them full of letters in reverse order and verifies.
int main(int argc, char** argv) {
    printf("starting\n");
    int fd = 0;
    fd = open("/dev/class/pci/004/00:14:00/xhci_usb/usb_bus/usb-dev-002/usb_mass_storage", O_RDWR);
    if (fd < 0) {
        printf("msd_test: cannot open '%d'\n", fd);
        return -1;
    }

    const char* abc = "abcdefghijklmnopqrstuvwxyz";
    int status = write_read_pattern_test(fd, abc, 1024, 0);
    if (status != 0) {
        printf("TEST FAILURE: written data and read data do not match\n");
        return -1;
    } else {
        printf("TEST PASSED\n");
    }

    const char* zyx = "zyxwvutsrqponmlkjihgfedcba";
    status = write_read_pattern_test(fd, zyx, 512, 1024);
    if (status != 0) {
        printf("TEST FAILURE: written data and read data do not match\n");
    } else {
        printf("TEST PASSED\n");
    }

    const char* asdf = "asdf";
    status = write_read_pattern_test(fd, asdf, 5120, 5120);
    if (status != 0) {
        printf("TEST FAILURE: written data and read data do not match\n");
    } else {
        printf("TEST PASSED\n");
    }

    return status;


}
