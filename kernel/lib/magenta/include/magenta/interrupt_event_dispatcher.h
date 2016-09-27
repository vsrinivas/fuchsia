// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <magenta/interrupt_dispatcher.h>
#include <mxtl/intrusive_wavl_tree.h>
#include <sys/types.h>

class InterruptEventDispatcher final : public InterruptDispatcher {
public:
    static status_t Create(uint32_t vector,
                           uint32_t flags,
                           mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    InterruptEventDispatcher(const InterruptDispatcher &) = delete;
    InterruptEventDispatcher& operator=(const InterruptDispatcher &) = delete;

    ~InterruptEventDispatcher() final;
    status_t InterruptComplete() final;

    // requred to exist in our collection of allocated vectors.
    uint32_t GetKey() const { return vector_; }

private:
    using VectorCollection = mxtl::WAVLTree<uint32_t, InterruptEventDispatcher*>;
    friend mxtl::DefaultWAVLTreeTraits<InterruptEventDispatcher*>;

    explicit InterruptEventDispatcher(uint32_t vector) : vector_(vector) { }

    static enum handler_return IrqHandler(void* ctx);

    const uint32_t vector_;
    mxtl::WAVLTreeNodeState<InterruptEventDispatcher*> wavl_node_state_;

    static Mutex vectors_lock_;
    static VectorCollection vectors_;
};
