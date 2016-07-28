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

#ifndef MUTEX_H_
#define MUTEX_H_

#include "magma_util/macros.h"
#include <mutex>

namespace magma {

class Mutex : public std::mutex {
public:
    Mutex() : locked_(false) {}

    void lock()
    {
        DASSERT(!locked_);
        std::mutex::lock();
        locked_ = true;
    }
    void unlock()
    {
        DASSERT(locked_);
        std::mutex::unlock();
        locked_ = false;
    }
    bool try_lock()
    {
        DASSERT(!locked_);
        if (std::mutex::try_lock()) {
            locked_ = true;
            return true;
        }
        return false;
    }
    bool is_locked() { return locked_; }

private:
    bool locked_;
};

class LockGuard : public std::lock_guard<Mutex> {
public:
    LockGuard(Mutex& mutex) : std::lock_guard<Mutex>(mutex) {}
};

} // namespace magma

#endif // MUTEX_H_
