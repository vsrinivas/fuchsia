// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mxio/io.h>
#include <mxio/util.h>

#include <magenta/syscalls.h>

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

    int rtc_fd = open("/dev/misc/rtc", O_RDONLY);
    if (rtc_fd < 0) {
        printf("cannot open rtc device\n");
        return -1;
    }

    for (;;) {
        char* time = get_rtc_time(rtc_fd);
        if (time)
            print_at(gen_text(time), 1, 50);
        mx_nanosleep(MX_SEC(1));
    }

    return 0;
}
