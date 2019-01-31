// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/device/serial.h>

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#define DEV_SERIAL "/dev/class/serial"

static void serial_print(int fd, const char* str) {
    write(fd, str, strlen(str));
}

int main(int argc, char** argv) {
    struct dirent* de;
    DIR* dir = opendir(DEV_SERIAL);
    if (!dir) {
        printf("Error opening %s\n", DEV_SERIAL);
        return -1;
    }

    int fd = -1;
   char path[100];

    while ((de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", DEV_SERIAL, de->d_name);
        fd = open(path, O_RDWR);
        if (fd < 0) {
            continue;
        }

        uint32_t serial_class;
        if (ioctl_serial_get_class(fd, &serial_class) < 0 || serial_class != SERIAL_CLASS_GENERIC) {
            close(fd);
            continue;
        } else {
            break;
        }
    }

    if (fd < 0) {
        fprintf(stderr, "could not find generic serial port in %s\n", DEV_SERIAL);
        return -1;
    }

    while (1) {
        char buffer[100];
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            fprintf(stderr, "serial read failed: %s\n", strerror(errno));
            break;
        }

        // echo text locally
        for (ssize_t i = 0; i < count; i++) {
            printf("%c", buffer[i]);
        }
        fflush(stdout);

        if (buffer[0] == 'x' || buffer[0] == 'X') {
            serial_print(fd, "Closing and reopening the serial port. Wish me luck!\n");
            // wait for data to be written before closing handle
            // TODO(voydanoff) eliminate this sleep after we implement socket_flush()
            sleep(1);
            close(fd);
            // wait a bit for serial port to shut down before reopening
            sleep(1);
            fd = open(path, O_RDWR);
             if (fd < 0) {
                fprintf(stderr, "failed to reopen serial port: %s\n", strerror(errno));
                return fd;
            }
            serial_print(fd, "...and we're back!\n");
        } else {
            serial_print(fd, "Read: \"");
            write(fd, buffer, count);
            serial_print(fd, "\"\n");
       }
    }

    close(fd);

    return 0;
}
