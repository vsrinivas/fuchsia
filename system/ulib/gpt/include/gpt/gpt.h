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

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct gpt_device gpt_device_t;

#define PARTITIONS_COUNT 128

typedef struct gpt_partition {
    uint8_t type[16];
    uint8_t guid[16];
    uint64_t first;
    uint64_t last;
    uint64_t flags;
    uint8_t name[72];
} gpt_partition_t;

struct gpt_device {
    bool valid;
    // true if the partition table on the device is valid
    gpt_partition_t* partitions[PARTITIONS_COUNT];
    // pointer to a list of partitions
};

int gpt_device_init(int fd, uint64_t blocksize, uint64_t blocks, gpt_device_t** out_dev);
// read the partition table from the device.

void gpt_device_release(gpt_device_t* dev);
// releases the device

int gpt_device_sync(gpt_device_t* dev);
// writes the partition table to the device. it is the caller's responsibility to
// rescan partitions for the block device if needed

int gpt_partition_add(gpt_device_t* dev, const char* name, uint8_t* type, uint8_t* guid, uint64_t offset, uint64_t blocks, uint64_t flags);
// adds a partition

int gpt_partition_remove(gpt_device_t* dev, const uint8_t* guid);
// removes a partition
