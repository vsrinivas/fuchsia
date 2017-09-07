// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>
#include <fbl/canary.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <object/interrupt_dispatcher.h>
#include <sys/types.h>

class InterruptEventDispatcher final : public InterruptDispatcher {
public:
    static mx_status_t Create(uint32_t vector,
                              uint32_t flags,
                              fbl::RefPtr<Dispatcher>* dispatcher,
                              mx_rights_t* rights);

    InterruptEventDispatcher(const InterruptDispatcher &) = delete;
    InterruptEventDispatcher& operator=(const InterruptDispatcher &) = delete;

    ~InterruptEventDispatcher() final;
    mx_status_t InterruptComplete() final;
    mx_status_t UserSignal() final;

    // requred to exist in our collection of allocated vectors.
    uint32_t GetKey() const { return vector_; }

private:
    using VectorCollection = fbl::WAVLTree<uint32_t, InterruptEventDispatcher*>;
    friend fbl::DefaultWAVLTreeTraits<InterruptEventDispatcher*>;

    explicit InterruptEventDispatcher(uint32_t vector) : vector_(vector) { }

    static enum handler_return IrqHandler(void* ctx);

    fbl::Canary<fbl::magic("INED")> canary_;
    const uint32_t vector_;
    fbl::WAVLTreeNodeState<InterruptEventDispatcher*> wavl_node_state_;

    static fbl::Mutex vectors_lock_;
    static VectorCollection vectors_ TA_GUARDED(vectors_lock_);
};
