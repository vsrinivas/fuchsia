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

#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include <magenta/syscalls.h>

#define MAX_LOG_LINE (MX_LOG_RECORD_MAX + 32)

static mx_handle_t loghandle;

int get_log_line(char* out) {
    char buf[MX_LOG_RECORD_MAX + 1];
    mx_log_record_t* rec = (mx_log_record_t*)buf;
    if (_magenta_log_read(loghandle, MX_LOG_RECORD_MAX, rec, 0) > 0) {
        if (rec->datalen && (rec->data[rec->datalen - 1] == '\n')) {
            rec->datalen--;
        }
        rec->data[rec->datalen] = 0;
        snprintf(out, MAX_LOG_LINE, "[%05d.%03d] %c %s\n",
                 (int)(rec->timestamp / 1000000000ULL),
                 (int)((rec->timestamp / 1000000ULL) % 1000ULL),
                 (rec->flags & MX_LOG_FLAG_KERNEL) ? 'K' : 'U',
                 rec->data);
        return strlen(out);
    } else {
        return 0;
    }
}

#define MAX_LOG_DATA 1280

typedef struct logpacket {
    uint32_t magic;
    uint32_t seqno;
    char data[MAX_LOG_DATA];
} logpacket_t;

static volatile uint32_t seqno = 1;
static volatile uint32_t pending = 0;

void udp6_recv(void* data, size_t len,
               const ip6_addr* daddr, uint16_t dport,
               const ip6_addr* saddr, uint16_t sport) {
    if (dport != 33338)
        return;
    if (len != 8)
        return;
    logpacket_t* pkt = data;
    if (pkt->magic != 0xaeae1123)
        return;
    if (pkt->seqno != seqno)
        return;
    seqno++;
    pending = 0;
    printf("ack!\n");
}

int main(int argc, char** argv) {
    logpacket_t pkt;
    int len = 0;
    if ((loghandle = _magenta_log_create(0)) < 0) {
        return -1;
    }
    _magenta_nanosleep(1000000000ULL);
    printf("netsvc: main()\n");
    while (netifc_open() < 0) {
        _magenta_nanosleep(100000000ULL);
    }
    printf("netsvc: start\n");
    for (;;) {
        if (pending == 0) {
            pkt.magic = 0xaeae1123;
            pkt.seqno = seqno;
            len = 0;
            while (len < (MAX_LOG_DATA - MAX_LOG_LINE)) {
                int r = get_log_line(pkt.data + len);
                if (r > 0) {
                    len += r;
                } else {
                    break;
                }
            }
            if (len) {
                len += 8;
                pending = 1;
                goto transmit;
            }
        }
        if (netifc_timer_expired()) {
        transmit:
            udp6_send(&pkt, 8 + len, &ip6_ll_all_nodes, 33337, 33338);
            netifc_set_timer(100);
        }
        netifc_poll();
    }
    return 0;
}
