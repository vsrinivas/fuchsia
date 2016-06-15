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

#include <magenta/types.h>
#include <runtime/mutex.h>
#include <sys/types.h>

#define FIFOSIZE 256
#define FIFOMASK (FIFOSIZE - 1)

typedef struct fifo {
    uint8_t data[FIFOSIZE];
    uint32_t head;
    uint32_t tail;
    mxr_mutex_t lock;
} fifo_t;

mx_status_t fifo_read(fifo_t* fifo, uint8_t* out);
void fifo_write(fifo_t* fifo, uint8_t x);
