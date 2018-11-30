// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <kernel/mutex.h>
#include <vm/vm.h>
#include <zircon/types.h>

// Callback to whatever is backing the PageSource.
class PageSourceCallback {
public:
    // OnClose should be called once no more requests will be made to the page source. The
    // callback can keep a reference to the page source, so it must be called outside of
    // the PageSource destructor.
    virtual void OnClose() = 0;
};

// Object which bridges a vm_object to some external data source.
class PageSource : public fbl::RefCounted<PageSource> {
public:
    PageSource(PageSourceCallback* callback, uint64_t page_source_id);
    ~PageSource();

    // Closes the source. All pending transactions will be aborted and all future
    // calls will fail.
    void Close();

    // Gets an id used for ownership verification.
    uint64_t get_page_source_id() const { return page_source_id_; }

private:
    PageSourceCallback* const callback_;
    const uint64_t page_source_id_;

    fbl::Mutex mtx_;
    bool closed_ TA_GUARDED(mtx_) = false;
};
