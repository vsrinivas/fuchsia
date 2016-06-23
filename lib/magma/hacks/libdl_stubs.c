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

// These functions are designed to be nonbreaking stubs of libdl functions so all
// so all the random places where mesa uses them can build

#include <stdio.h>

void* dlopen(const char* filename, int flag)
{
    fprintf(stderr, "Warning: Mesa hitting dlopen stub, this may cause problems\n");
    return NULL;
}

char* dlerror(void)
{
    fprintf(stderr, "Warning: Mesa hitting dlerror stub, this may cause problems\n");
    return NULL;
}

void* dlsym(void* handle, const char* symbol)
{
    fprintf(stderr, "Warning: Mesa hitting dlsym stub, this may cause problems\n");
    return NULL;
}

int dlclose(void* handle)
{
    fprintf(stderr, "Warning: Mesa hitting dlclose stub, this may cause problems\n");
    return 0;
}

int dladdr(void* addr, void* info)
{
    fprintf(stderr, "Warning: Mesa hitting dladdr stub, this may cause problems\n");
    return 0;
}
