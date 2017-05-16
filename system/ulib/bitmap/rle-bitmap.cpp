// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitmap/rle-bitmap.h>

#include <stddef.h>

#include <magenta/errors.h>
#include <magenta/types.h>
#include <mxalloc/new.h>
#include <mxtl/algorithm.h>

namespace bitmap {

namespace {

// Allocate a new bitmap element.  If *free_list* is null, allocate one using
// new.  If *free_list* is not null, take one from *free_list*.
mxtl::unique_ptr<RleBitmapElement> AllocateElement(RleBitmap::FreeList* free_list) {
    if (!free_list) {
        AllocChecker ac;
        mxtl::unique_ptr<RleBitmapElement> new_elem(new (&ac) RleBitmapElement());
        if (!ac.check()) {
            return mxtl::unique_ptr<RleBitmapElement>();
        }
        return new_elem;
    } else {
        return free_list->pop_front();
    }
}

// Release the element *elem*.  If *free_list* is null, release the element
// with delete.  If *free_list* is not null, append it to *free_list*.
void ReleaseElement(RleBitmap::FreeList* free_list, mxtl::unique_ptr<RleBitmapElement>&& elem) {
    if (free_list) {
        free_list->push_back(mxtl::move(elem));
    }
}

} // namespace

bool RleBitmap::Get(size_t bitoff, size_t bitmax, size_t* first_unset) const {
    for (auto& elem : elems_) {
        if (bitoff < elem.bitoff) {
            break;
        }
        if (bitoff < elem.bitoff + elem.bitlen) {
            bitoff = elem.bitoff + elem.bitlen;
            break;
        }
    }
    if (bitoff > bitmax) {
        bitoff = bitmax;
    }
    if (first_unset) {
        *first_unset = bitoff;
    }
    return bitoff == bitmax;
}

void RleBitmap::ClearAll() {
    elems_.clear();
    num_elems_ = 0;
}

mx_status_t RleBitmap::Set(size_t bitoff, size_t bitmax) {
    return SetInternal(bitoff, bitmax, nullptr);
}

mx_status_t RleBitmap::SetNoAlloc(size_t bitoff, size_t bitmax, FreeList* free_list) {
    if (free_list == nullptr) {
        return ERR_INVALID_ARGS;
    }

    return SetInternal(bitoff, bitmax, free_list);
}

mx_status_t RleBitmap::Clear(size_t bitoff, size_t bitmax) {
    return ClearInternal(bitoff, bitmax, nullptr);
}

mx_status_t RleBitmap::ClearNoAlloc(size_t bitoff, size_t bitmax, FreeList* free_list) {
    if (free_list == nullptr) {
        return ERR_INVALID_ARGS;
    }

    return ClearInternal(bitoff, bitmax, free_list);
}

mx_status_t RleBitmap::SetInternal(size_t bitoff, size_t bitmax, FreeList* free_list) {
    if (bitmax < bitoff) {
        return ERR_INVALID_ARGS;
    }

    const size_t bitlen = bitmax - bitoff;
    if (bitlen == 0) {
        return NO_ERROR;
    }

    mxtl::unique_ptr<RleBitmapElement> new_elem = AllocateElement(free_list);
    if (!new_elem) {
        return ERR_NO_MEMORY;
    }
    ++num_elems_;
    new_elem->bitoff = bitoff;
    new_elem->bitlen = bitlen;

    auto ends_after = elems_.find_if([bitoff](const RleBitmapElement& elem) -> bool {
        return elem.bitoff + elem.bitlen >= bitoff;
    });

    // Insert the new element before the first node that ends at a point >=
    // when we begin.
    elems_.insert(ends_after, mxtl::move(new_elem));

    // If ends_after was the end of the list, there is no merging to do.
    if (ends_after == elems_.end()) {
        return NO_ERROR;
    }

    auto itr = ends_after;
    RleBitmapElement& elem = *--ends_after;

    if (elem.bitlen + elem.bitoff >= itr->bitoff) {
        // Our range either starts before or in the middle/end of *elem*.
        // Adjust it so it starts at the same place as *elem*, to allow
        // the merge logic to not consider this overlap case.
        elem.bitlen += elem.bitoff - itr->bitoff;
        elem.bitoff = itr->bitoff;
    }

    // Walk forwards and remove/merge any overlaps
    size_t max = elem.bitoff + elem.bitlen;
    while (itr != elems_.end()) {
        if (itr->bitoff > max) {
            break;
        }

        max = mxtl::max(max, itr->bitoff + itr->bitlen);
        elem.bitlen = max - elem.bitoff;

        auto to_erase = itr;
        ++itr;
        ReleaseElement(free_list, elems_.erase(to_erase));
        --num_elems_;
    }

    return NO_ERROR;
}

mx_status_t RleBitmap::ClearInternal(size_t bitoff, size_t bitmax, FreeList* free_list) {
    if (bitmax < bitoff) {
        return ERR_INVALID_ARGS;
    }

    if (bitmax - bitoff == 0) {
        return NO_ERROR;
    }

    auto itr = elems_.begin();
    while (itr != elems_.end()) {
        if (itr->bitoff + itr->bitlen < bitoff) {
            ++itr;
            continue;
        }
        if (bitmax < itr->bitoff) {
            break;
        }
        if (itr->bitoff < bitoff) {
            if (itr->bitoff + itr->bitlen <= bitmax) {
                // '*itr' contains 'bitoff'.
                itr->bitlen = bitoff - itr->bitoff;
                ++itr;
                continue;
            } else {
                // '*itr' contains [bitoff, bitmax), and we need to split it.
                mxtl::unique_ptr<RleBitmapElement> new_elem = AllocateElement(free_list);
                if (!new_elem) {
                    return ERR_NO_MEMORY;
                }
                ++num_elems_;
                new_elem->bitoff = bitmax;
                new_elem->bitlen = itr->bitoff + itr->bitlen - bitmax;

                elems_.insert_after(itr, mxtl::move(new_elem));
                itr->bitlen = bitoff - itr->bitoff;
                break;
            }
        } else {
            if (bitmax < itr->bitoff + itr->bitlen) {
                // 'elem' contains 'bitmax'
                itr->bitlen = itr->bitoff + itr->bitlen - bitmax;
                itr->bitoff = bitmax;
                break;
            } else {
                // [bitoff, bitmax) fully contains '*itr'.
                auto to_erase = itr++;
                ReleaseElement(free_list, elems_.erase(to_erase));
                --num_elems_;
            }
        }
    }
    return NO_ERROR;
}

} // namespace bitmap
