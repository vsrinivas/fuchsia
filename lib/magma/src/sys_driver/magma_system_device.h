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

#ifndef _MAGMA_SYSTEM_DEVICE_H_
#define _MAGMA_SYSTEM_DEVICE_H_

#include "magma_system_buffer.h"
#include "magma_system_context.h"
#include "msd.h"

#include <map>
#include <memory>

struct MagmaSystemDevice {
public:
    MagmaSystemDevice(msd_device* msd_dev) : msd_dev_(msd_dev) {}

    msd_device* arch() { return msd_dev_; }

    void set_client_id(msd_client_id client_id) { client_id_ = client_id; }
    msd_client_id client_id() { return client_id_; }

    // Allocates a buffer of at least the requested size and adds it to the
    // buffer map. Returns a shared_ptr to the allocated buffer on success
    // or nullptr on allocation failure
    std::shared_ptr<MagmaSystemBuffer> AllocateBuffer(uint64_t size);
    // Attempts to locate a buffer by handle in the buffer map and return it.
    // Returns nullptr if the buffer is not found
    std::shared_ptr<MagmaSystemBuffer> LookupBuffer(uint32_t handle);
    // This removes the reference to the shared_ptr in the map
    // other instances remain valid until deleted
    // Returns false if no buffer with the given handle exists in the map
    bool FreeBuffer(uint32_t handle);

    bool CreateContext(uint32_t* context_id_out);
    bool DestroyContext(uint32_t context_id);

    // Returns the device id. 0 is invalid.
    uint32_t GetDeviceId();

private:
    msd_client_id client_id_{};
    msd_device* msd_dev_;
    std::map<uint32_t, std::shared_ptr<MagmaSystemBuffer>> buffer_map_;
    std::map<uint32_t, std::unique_ptr<MagmaSystemContext>> context_map_;
    uint32_t next_context_id_{};
};

#endif //_MAGMA_SYSTEM_DEVICE_H_
