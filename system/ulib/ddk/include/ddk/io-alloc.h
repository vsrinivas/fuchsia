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

#include <stddef.h>

typedef struct io_alloc io_alloc_t;

io_alloc_t* io_alloc_init(size_t size);
void io_alloc_free(io_alloc_t* ioa);

void* io_malloc(io_alloc_t* ioa, size_t size);
void* io_calloc(io_alloc_t* ioa, size_t count, size_t size);
void* io_memalign(io_alloc_t* ioa, size_t align, size_t size);
void io_free(io_alloc_t* ioa, void* ptr);
mx_paddr_t io_virt_to_phys(io_alloc_t* ioa, mx_vaddr_t virt_addr);
mx_vaddr_t io_phys_to_virt(io_alloc_t* ioa, mx_paddr_t phys_addr);
