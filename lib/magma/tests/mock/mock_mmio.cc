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

#include "mock/mock_mmio.h"
#include "magma_util/dlog.h"
#include <stdlib.h>

std::unique_ptr<MockMmio> MockMmio::Create(uint64_t size)
{
    void* addr = malloc(size);
    return std::unique_ptr<MockMmio>(new MockMmio(addr, size));
}

MockMmio::MockMmio(void* addr, uint64_t size) : magma::PlatformMmio(addr, size) {}

MockMmio::~MockMmio()
{
    DLOG("MockMmio dtor");
    free(addr());
}
