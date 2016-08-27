// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>
#include <utils/ref_counted_internal.h>

namespace mxtl {

// Base class for a reference counted type.  Use as
//
// class Handle : public RefCounted<Handle> {
// };
//
// This supports intrusive atomic reference counting with adoption. This means
// that a new object starts life at a reference count of 1 and has to be adopted
// by a type (such as a mxtl::RefPtr) that begins manipulation of the reference
// count. If the reference count ever reaches zero, the object's lifetime is
// over and it should be destroyed (Release() returns true if this is the case).
template <typename T>
class RefCounted : public internal::RefCountedBase {
public:
    RefCounted() {}
    ~RefCounted() {}

    using internal::RefCountedBase::AddRef;
    using internal::RefCountedBase::Release;

#if (LK_DEBUGLEVEL > 1)
    using internal::RefCountedBase::Adopt;
#endif

private:
    RefCounted(const RefCounted&) = delete;
    RefCounted& operator=(const RefCounted&) = delete;
};

}  // namespace mxtl
