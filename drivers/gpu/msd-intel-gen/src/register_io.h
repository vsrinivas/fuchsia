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

#ifndef REGISTER_IO_H
#define REGISTER_IO_H

#include "magma_util/platform_mmio.h"
#include <memory>

// RegisterIo wraps mmio access and adds forcewake logic.
class RegisterIo {
public:
    RegisterIo(std::unique_ptr<magma::PlatformMmio> mmio);

    void Write32(uint32_t offset, uint32_t val) { mmio_->Write32(val, offset); }

    uint32_t Read32(uint32_t offset) { return mmio_->Read32(offset); }

    uint64_t Read64(uint32_t offset) { return mmio_->Read64(offset); }

    magma::PlatformMmio* mmio() { return mmio_.get(); }

private:
    std::unique_ptr<magma::PlatformMmio> mmio_;
};

#endif // REGISTER_IO_H
