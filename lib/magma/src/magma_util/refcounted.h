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
