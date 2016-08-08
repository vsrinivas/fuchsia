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

#include <mxio/io.h>

int main(int argc, char** argv) {
    printf("starting\n");
    unsigned char x;
    int fd = 0;
    fd = open("/dev/class/usb-msd/usb_mass_storage", O_RDWR);
    if (fd < 0) {
        printf("msd_test: cannot open '%d'\n", fd);
        return -1;
    }
    int w = write(fd, &x, 1);
    printf("w was: %02x\n", w);
    // usleep(100);

    char out[18] = {'a', 'b','c','d','e', 'f', 'g', 'h', 'i',
                        'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r'};
        printf("here are original\n");
    for (int i = 0; i < 36; i++) {
        printf("%02x ", ((unsigned char*)out)[i]);
    }
    printf("\n");
    fflush(stdout);
    printf("waiting\n");
    // usleep(5000000);
    mxio_wait_fd(fd, MXIO_EVT_READABLE, NULL, MX_TIME_INFINITE);
    printf("got to read\n");
    // printf("read status: %02x \n", ((uint)read(fd, &out, 36)));
    // read(fd, &out, 10);

    printf("here are results\n");
    // printf("here is first one as int before thing %d \n", ((int*)out)[0]);
    // int* a =(int*)out;
    // int* b =(int*)(out+1);
    // *a = 1;
    // *b = 15;
    for (int i = 0; i < 36; i++) {
        printf("%02x ", ((unsigned char*)out)[i]);
    }
    printf("\n");
    fflush(stdout);

    printf("\n");
    return 0;
}
