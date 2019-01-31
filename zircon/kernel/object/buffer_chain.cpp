// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/buffer_chain.h>

// Makes a const void* look like a user_in_ptr<const void>.
//
// Sometimes we need to copy data from kernel space. KernelPtrAdapter allows us to implement the
// copy logic once for both const void* and user_in_ptr<const void>.
class KernelPtrAdapter {
public:
    explicit KernelPtrAdapter(const void* p)
        : p_(p) {}
    zx_status_t copy_array_from_user(void* dst, size_t count) const {
        memcpy(dst, p_, count);
        return ZX_OK;
    }
    KernelPtrAdapter byte_offset(size_t offset) const {
        return KernelPtrAdapter(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p_) + offset));
    }

private:
    const void* p_;
};

zx_status_t BufferChain::CopyInKernel(const void* src, size_t dst_offset, size_t size) {
    return CopyInCommon(KernelPtrAdapter(src), dst_offset, size);
}

template zx_status_t BufferChain::CopyInCommon(user_in_ptr<const void> src, size_t dst_offset,
                                               size_t size);
