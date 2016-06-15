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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/types.h>

#define BOOTFS_MAX_NAME_LEN 256

static const char FSMAGIC[16] = "[BOOTFS]\0\0\0\0\0\0\0\0";

// BOOTFS is a trivial "filesystem" format
//
// It has a 16 byte magic/version value (FSMAGIC)
// Followed by a series of records of:
//   namelength (32bit le)
//   filesize   (32bit le)
//   fileoffset (32bit le)
//   namedata   (namelength bytes, includes \0)
//
// - fileoffsets must be page aligned (multiple of 4096)

#define NLEN 0
#define FSIZ 1
#define FOFF 2

void bootfs_parse(void* _data, int len,
                  void (*cb)(const char* fn, size_t off, size_t len)) {
    uint8_t* data = _data;
    uint8_t* end = data + len;
    char name[BOOTFS_MAX_NAME_LEN];
    uint32_t header[3];

    if (memcmp(data, FSMAGIC, sizeof(FSMAGIC))) {
        return;
    }
    data += sizeof(FSMAGIC);

    while ((end - data) > (int)sizeof(header)) {
        memcpy(header, data, sizeof(header));
        data += sizeof(header);

        // check for end marker
        if (header[NLEN] == 0)
            break;

        // require reasonable filename size
        if ((header[NLEN] < 2) || (header[NLEN] > BOOTFS_MAX_NAME_LEN)) {
            break;
        }

        // require correct alignment
        if (header[FOFF] & 4095) {
            break;
        }

        if ((end - data) < (off_t)header[NLEN]) {
            break;
        }
        memcpy(name, data, header[NLEN]);
        data += header[NLEN];
        name[header[NLEN] - 1] = 0;

        cb(name, header[FOFF], header[FSIZ]);
    }
}
