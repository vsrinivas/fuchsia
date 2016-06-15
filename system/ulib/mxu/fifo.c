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

#include <mxu/fifo.h>

mx_status_t fifo_read(fifo_t* fifo, uint8_t* out) {
    if (fifo->head == fifo->tail) {
        return -1;
    }
    *out = fifo->data[fifo->tail];
    fifo->tail = (fifo->tail + 1) & FIFOMASK;
    return NO_ERROR;
}

void fifo_write(fifo_t* fifo, uint8_t x) {
    uint32_t next = (fifo->head + 1) & FIFOMASK;
    if (next != fifo->tail) {
        fifo->data[fifo->head] = x;
        fifo->head = next;
    }
}
