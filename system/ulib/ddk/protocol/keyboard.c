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

#include <ddk/protocol/keyboard.h>
#include <string.h>

mx_status_t mx_key_fifo_peek(mx_key_fifo_t* fifo, mx_key_event_t** out) {
    if (fifo->head == fifo->tail)
        return -1;
    *out = &fifo->events[fifo->tail];
    return NO_ERROR;
}

mx_status_t mx_key_fifo_read(mx_key_fifo_t* fifo, mx_key_event_t* out) {
    if (fifo->head == fifo->tail)
        return -1;
    if (out)
        memcpy(out, &fifo->events[fifo->tail], sizeof(mx_key_event_t));
    fifo->tail = (fifo->tail + 1) & FIFOMASK;
    return NO_ERROR;
}

mx_status_t mx_key_fifo_write(mx_key_fifo_t* fifo, mx_key_event_t* ev) {
    uint32_t next = (fifo->head + 1) & FIFOMASK;
    if (next != fifo->tail) {
        memcpy(&fifo->events[fifo->head], ev, sizeof(mx_key_event_t));
        fifo->head = next;
    }
    return NO_ERROR;
}
