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

#include "option.h"
#include "util.h"

#pragma GCC visibility push(hidden)

#include <string.h>

#pragma GCC visibility pop

#define OPTION_DEFAULT(option) \
    case OPTION_##option: value = OPTION_##option##_DEFAULT; break

static void initialize_options(struct options* o) {
    for (enum option i = 0; i < OPTION_MAX; ++i) {
        const char* value;
        switch (i) {
            OPTION_DEFAULT(FILENAME);
            OPTION_DEFAULT(SHUTDOWN);
        case OPTION_MAX:
            __builtin_unreachable();
        }
        o->value[i] = value;
    }
}

#define OPTION_STRING(option)                               \
    case OPTION_##option:                                   \
        string = OPTION_##option##_STRING;                  \
        string_len = sizeof(OPTION_##option##_STRING) - 1;  \
        break

static void apply_option(struct options* o, const char* arg) {
    size_t len = strlen(arg);
    for (enum option i = 0; i < OPTION_MAX; ++i) {
        const char* string;
        size_t string_len;
        switch (i) {
            OPTION_STRING(FILENAME);
            OPTION_STRING(SHUTDOWN);
        case OPTION_MAX:
            __builtin_unreachable();
        }
        if (len > string_len &&
            arg[string_len] == '=' &&
            !strncmp(arg, string, string_len)) {
            o->value[i] = &arg[string_len + 1];
        }
    }
}

void parse_options(mx_handle_t log, struct options *o, char** strings) {
    initialize_options(o);
    for (char** sp = strings; *sp != NULL; ++sp) {
        const char* arg = *sp;
        print(log, "option \"", arg, "\"\n", NULL);
        apply_option(o, arg);
    }
}
