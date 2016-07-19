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
#include <string.h>

#include <mxio/io.h>
#include <mxio/util.h>

#include <magenta/syscalls.h>

#define  ONE_SEC (1000 * 1000 * 1000)

#define  VT_ESC "\033"
#define  SAVE_CUR  VT_ESC "7"
#define  REST_CUR  VT_ESC "8"
#define  MOVE_CUR  VT_ESC "[%d;%df"

void print_at(const char* text, int v, int h) {
    static char buf[64] = {0};
    if (!text)
        return;
    int s = snprintf(buf, sizeof(buf), SAVE_CUR MOVE_CUR "%s" REST_CUR, v, h, text);
    if (s < 0)
        return;
    write(STDOUT_FILENO, buf, s);
}

char* get_rtc_time(int fd) {
    static char time[12] = {0};
    int r = read(fd, time, sizeof(time));
    return (r > 1) ? time : NULL;
}

char* gen_text(char* time) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "[%s]", time);
    return buf;
}

int main(int argc, char** argv) {
    printf("=@ clock @=\n");

    int rtc_fd = open("dev/rtc", O_RDONLY);
    if (rtc_fd < 0) {
        printf("cannot open rtc device\n");
        return -1;
    }

    for (;;) {
        char* time = get_rtc_time(rtc_fd);
        if (time)
            print_at(gen_text(time), 1, 50);
        mx_nanosleep(1u * ONE_SEC);
    }

    return 0;
}
