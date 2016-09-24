// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __NEW_H
#define __NEW_H

#include <sys/types.h>

class AllocChecker {
public:
    AllocChecker();
    ~AllocChecker();
    void arm(size_t sz, bool result);
    bool check();
private:
    unsigned state_;
};

void *operator new(size_t, AllocChecker* ac) noexcept;
void *operator new(size_t, void *ptr);
void *operator new[](size_t, AllocChecker* ac) noexcept;
void *operator new[](size_t, void *ptr);
void operator delete(void *p);
void operator delete[](void *p);
void operator delete(void *p, size_t);
void operator delete[](void *p, size_t);

#endif
