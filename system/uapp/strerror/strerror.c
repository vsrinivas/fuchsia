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

#include <errno.h>
#include <mojo/mojo_string.h>
#include <runtime/status.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    for (int idx = 1; idx < argc; idx++) {
        errno = 0;
        long error_long = strtol(argv[idx], NULL, 10);
        if (errno)
            exit(ERR_INVALID_ARGS);
        int error = (int)error_long;
        const char* mx_error = mx_strstatus((mx_status_t)error);
        const char* mojo_error = mojo_strerror((mojo_result_t)error);
        char* posix_error = strerror(error);
        printf("Int value: %d\n", error);
        printf("\tMagenta error: %s\n", mx_error);
        printf("\tMojo error: %s\n", mojo_error);
        printf("\tPosix error: %s\n", posix_error);
    }
}
