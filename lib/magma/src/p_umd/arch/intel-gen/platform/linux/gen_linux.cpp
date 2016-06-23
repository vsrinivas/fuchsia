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

#include "p_umd/arch/intel-gen/include/intel_gen.h"

#include <errno.h>
#include <fcntl.h>
#include <i915_drm.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "util/dlog.h"

#define memclear(s) memset(&s, 0, sizeof(s))

class GenLinux : public IntelGen {
public:
    GenLinux(int fd) : fd_(fd) { ReadPciDeviceId(); }

    ~GenLinux() override {}

    void Close()
    {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    uint64_t GetPciDeviceId() override { return device_id_; }

private:
    int Ioctl(unsigned long request, void* arg);
    void ReadPciDeviceId();

    int fd_;
    int device_id_;
};

int GenLinux::Ioctl(unsigned long request, void* arg)
{
    int ret;
    do {
        ret = ioctl(fd_, request, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
    return ret;
}

void GenLinux::ReadPciDeviceId()
{
    drm_i915_getparam_t gp;
    memclear(gp);
    gp.param = I915_PARAM_CHIPSET_ID;
    gp.value = &device_id_;

    int ret = Ioctl(DRM_IOCTL_I915_GETPARAM, &gp);
    if (ret) {
        DLOG("DRM_IOCTL_I915_GETPARAM returned %d errno %d\n", ret, errno);
        device_id_ = 0;
    } else if (device_id_ == 0) {
        DLOG("Got null device id\n");
    }
}

//////////////////////////////////////////////////////////////////////////////

IntelGen* magma_arch_open(uint32_t gpu_index)
{
    char gpu_device[64];
    snprintf(gpu_device, sizeof(gpu_device), "/dev/dri/card%d", gpu_index);

    int fd = open(gpu_device, O_RDWR);
    if (fd < 0) {
        DLOG("failed to open gpu_device: %s\n", gpu_device);
        return nullptr;
    }

    return new GenLinux(fd);
}

void magma_arch_close(IntelGen* gen)
{
    static_cast<GenLinux*>(gen)->Close();
    delete gen;
}
