// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


#include <mxio/io.h>

// change this number to change how many bytes are being written/read
#define TEST_LEN 1024

int write_read_pattern_test(int fd, const char* pattern) {
    char in[TEST_LEN];
    int pattern_len = strlen(pattern);
    // repeat this pattern across the buffer
    for (int i = 0; i < TEST_LEN; i += pattern_len) {
        memcpy(in + i, pattern, pattern_len);

    }
    memcpy(in + (pattern_len * (TEST_LEN/pattern_len)), pattern,
                 TEST_LEN % pattern_len);
    printf("Sending the write: \n");
    for (int i = 0; i < TEST_LEN; i++) {
        printf("%02x ", ((unsigned char*)in)[i]);
        if (i % 50 == 49) {
            printf("\n");
        }
    }
    printf("\n");
    int w = write(fd, &in, TEST_LEN);
    printf("the write returned: %02x\n", w);

    // seek back to start
    lseek(fd, -TEST_LEN, SEEK_CUR);
    char out[TEST_LEN];
    read(fd, out, TEST_LEN);
    printf("here is the results of the read\n");
    for (int i = 0; i < TEST_LEN; i++) {
        printf("%02x ", ((unsigned char*)out)[i]);
        if (i % 50 == 49) {
            printf("\n");
        }
    }
    printf("\n");
    printf("memcmp result: %i\n", memcmp(in, out, TEST_LEN));
    return memcmp(in, out, TEST_LEN);
}

// writes first block two blocks full of lowercase letters in order, then reads to verify.
// then writes them full of letters in reverse order and verifies.
int main(int argc, char** argv) {
    printf("starting\n");
    int fd = 0;
    fd = open("/dev/class/misc/usb_mass_storage", O_RDWR);
    if (fd < 0) {
        printf("msd_test: cannot open '%d'\n", fd);
        return -1;
    }

    const char* abc = "abcdefghijklmnopqrstuvwxyz";
    int status = write_read_pattern_test(fd, abc);

    if (status != 0) {
        printf("TEST FAILURE: written data and read data do not match\n");
        return -1;
    } else {
        printf("TEST PASSED\n");
    }

    const char* zyx = "zyxwvutsrqponmlkjihgfedcba";
    status = write_read_pattern_test(fd, zyx);
    if (status != 0) {
        printf("TEST FAILURE: written data and read data do not match\n");
    } else {
        printf("TEST PASSED\n");
    }

    return status;


}