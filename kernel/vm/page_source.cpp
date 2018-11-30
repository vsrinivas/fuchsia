// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/auto_lock.h>
#include <trace.h>
#include <vm/page_source.h>

#define LOCAL_TRACE 0

PageSource::PageSource(PageSourceCallback* callback, uint64_t page_source_id)
        : callback_(callback), page_source_id_(page_source_id) {
    LTRACEF("%p callback %p\n", this, callback_);
}

PageSource::~PageSource() {
    LTRACEF("%p\n", this);
    DEBUG_ASSERT(closed_);
}

void PageSource::Close() {
    fbl::AutoLock info_lock(&mtx_);
    LTRACEF("%p\n", this);

    if (!closed_) {
        closed_ = true;
        callback_->OnClose();
    }
}
