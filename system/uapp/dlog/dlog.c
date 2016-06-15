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
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>

int main(int argc, char** argv) {
    bool tail = false;
    mx_handle_t h;

    if ((argc == 2) && (!strcmp(argv[1], "-f"))) {
        tail = true;
    }
    if ((h = _magenta_log_create(0)) < 0) {
        printf("dlog: cannot open log\n");
    }

    char buf[MX_LOG_RECORD_MAX];
    mx_log_record_t* rec = (mx_log_record_t*)buf;
    for (;;) {
        if (_magenta_log_read(h, MX_LOG_RECORD_MAX, rec, tail ? MX_LOG_FLAG_WAIT : 0) > 0) {
            char tmp[64];
            snprintf(tmp, 64, "[%05d.%03d] %c ",
                     (int)(rec->timestamp / 1000000000ULL),
                     (int)((rec->timestamp / 1000000ULL) % 1000ULL),
                     (rec->flags & MX_LOG_FLAG_KERNEL) ? 'K' : 'U');
            write(1, tmp, strlen(tmp));
            write(1, rec->data, rec->datalen);
            write(1, "\n", 1);
        } else {
            break;
        }
    }
    return 0;
}