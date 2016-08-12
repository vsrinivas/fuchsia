// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REFCOUNTED_H_
#define REFCOUNTED_H_

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include <atomic>

namespace magma {

class Refcounted {
public:
    Refcounted(const char* name) : count_(1), name_(name) {}

    void Incref()
    {
        DASSERT(count_ > 0);
        if (++count_ < 2)
            DLOG("%s: Incref with initial count zero", name_);
    }

    void Decref()
    {
        DASSERT(count_ > 0);
        if (--count_ == 0) {
            DLOG("%s: Decref count now zero", name_);
            Delete();
        }
    }

    int Getref()
    {
        DASSERT(count_ > 0);
        return count_;
    }

    const char* name()
    {
        DASSERT(count_ > 0);
        return name_;
    };

protected:
    virtual ~Refcounted() {}

    virtual void Delete() { delete this; }

    std::atomic_int count_;
    const char* name_;
};

} // namespace magma

#endif // REFCOUNTED_H_
