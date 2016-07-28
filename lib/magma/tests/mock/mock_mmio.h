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

#ifndef MOCK_MMIO_H
#define MOCK_MMIO_H

#include "magma_util/platform_mmio.h"
#include <memory>

class MockMmio : public magma::PlatformMmio {
public:
    static std::unique_ptr<MockMmio> Create(uint64_t size);

    virtual ~MockMmio() override;

private:
    MockMmio(void* addr, uint64_t size);
};

#endif // MOCK_MMIO_H
