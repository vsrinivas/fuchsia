// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitmap/rle-bitmap.h>

#include <stddef.h>

#include <zircon/errors.h>
#include <zircon/types.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace bitmap {

namespace {

// Allocate a new bitmap element.  If *free_list* is null, allocate one using
// new.  If *free_list* is not null, take one from *free_list*.
fbl::unique_ptr<RleBitmapElement> AllocateElement(RleBitmap::FreeList* free_list) {
    if (!free_list) {
        fbl::AllocChecker ac;
        fbl::unique_ptr<RleBitmapElement> new_elem(new (&ac) RleBitmapElement());
        if (!ac.check()) {
            return fbl::unique_ptr<RleBitmapElement>();
        }
        return new_elem;
    } else {
        return free_list->pop_front();
    }
}

// Release the element *elem*.  If *free_list* is null, release the element
// with delete.  If *free_list* is not null, append it to *free_list*.
void ReleaseElement(RleBitmap::FreeList* free_list, fbl::unique_ptr<RleBitmapElement>&& elem) {
    if (free_list) {
        free_list->push_back(fbl::move(elem));
    }
}

} // namespace

zx_status_t RleBitmap::Find(bool is_set, size_t bitoff, size_t bitmax, size_t run_len, size_t* out)
            const {
    *out = bitmax;

    // Loop through all existing elems to try to find a |run_len| length range of |is_set| bits.
    // On each loop, |bitoff| is guaranteed to be either within the current elem, or in the range
    // of unset bits leading up to it.
    // Therefore, we can check whether |run_len| bits between |bitmax| and |bitoff| exist before
    // the start of the elem (for unset runs), or within the current elem (for set runs).
    for (const auto& elem : elems_) {
        if (bitoff >= elem.end()) {
            continue;
        } else if (bitmax - bitoff < run_len) {
            return ZX_ERR_NO_RESOURCES;
        }

        size_t elem_min = fbl::max(bitoff, elem.bitoff); // Minimum valid bit within elem.
        size_t elem_max = fbl::min(bitmax, elem.end()); // Maximum valid bit within elem.

        if (is_set && elem_max > elem_min && elem_max - elem_min >= run_len) {
            // This element contains at least |run_len| bits
            // which are between |bitoff| and |bitmax|.
            *out = elem_min;
            return ZX_OK;
        }

        if (!is_set && bitoff < elem.bitoff && elem.bitoff - bitoff >= run_len) {
            // There are at least |run_len| bits between |bitoff| and the beginning of this element.
            *out = bitoff;
            return ZX_OK;
        }

        if (bitmax < elem.end()) {
            // We have not found a valid run, and the specified range
            // does not extend past this element.
            return ZX_ERR_NO_RESOURCES;
        }

        // Update bitoff to the next value we want to check within the range.
        bitoff = elem.end();
    }


    if (!is_set && bitmax - bitoff >= run_len) {
        // We have not found an element with bits > bitoff, which means there is an infinite unset
        // range starting at bitoff.
        *out = bitoff;
        return ZX_OK;
    }

    return ZX_ERR_NO_RESOURCES;
}


bool RleBitmap::Get(size_t bitoff, size_t bitmax, size_t* first_unset) const {
    for (const auto& elem : elems_) {
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
    num_bits_ = 0;
}

zx_status_t RleBitmap::Set(size_t bitoff, size_t bitmax) {
    return SetInternal(bitoff, bitmax, nullptr);
}

zx_status_t RleBitmap::SetNoAlloc(size_t bitoff, size_t bitmax, FreeList* free_list) {
    if (free_list == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    return SetInternal(bitoff, bitmax, free_list);
}

zx_status_t RleBitmap::Clear(size_t bitoff, size_t bitmax) {
    return ClearInternal(bitoff, bitmax, nullptr);
}

zx_status_t RleBitmap::ClearNoAlloc(size_t bitoff, size_t bitmax, FreeList* free_list) {
    if (free_list == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    return ClearInternal(bitoff, bitmax, free_list);
}

zx_status_t RleBitmap::SetInternal(size_t bitoff, size_t bitmax, FreeList* free_list) {
    if (bitmax < bitoff) {
        return ZX_ERR_INVALID_ARGS;
    }

    const size_t bitlen = bitmax - bitoff;
    if (bitlen == 0) {
        return ZX_OK;
    }

    fbl::unique_ptr<RleBitmapElement> new_elem = AllocateElement(free_list);
    if (!new_elem) {
        return ZX_ERR_NO_MEMORY;
    }
    ++num_elems_;
    new_elem->bitoff = bitoff;
    new_elem->bitlen = bitlen;

    auto ends_after = elems_.find_if([bitoff](const RleBitmapElement& elem) -> bool {
        return elem.bitoff + elem.bitlen >= bitoff;
    });

    // Insert the new element before the first node that ends at a point >=
    // when we begin.
    elems_.insert(ends_after, fbl::move(new_elem));
    num_bits_ += bitlen;

    // If ends_after was the end of the list, there is no merging to do.
    if (ends_after == elems_.end()) {
        return ZX_OK;
    }

    auto itr = ends_after;
    RleBitmapElement& elem = *--ends_after;

    if (elem.bitoff >= itr->bitoff) {
        // Our range either starts before or in the middle/end of *elem*.
        // Adjust it so it starts at the same place as *elem*, to allow
        // the merge logic to not consider this overlap case.
        elem.bitlen += elem.bitoff - itr->bitoff;
        num_bits_ += elem.bitoff - itr->bitoff;
        elem.bitoff = itr->bitoff;
    }

    // Walk forwards and remove/merge any overlaps
    size_t max = elem.bitoff + elem.bitlen;
    while (itr != elems_.end()) {
        if (itr->bitoff > max) {
            break;
        }

        max = fbl::max(max, itr->bitoff + itr->bitlen);
        num_bits_ += max - elem.bitoff - itr->bitlen - elem.bitlen;
        elem.bitlen = max - elem.bitoff;
        auto to_erase = itr;
        ++itr;
        ReleaseElement(free_list, elems_.erase(to_erase));
        --num_elems_;
    }

    return ZX_OK;
}

zx_status_t RleBitmap::ClearInternal(size_t bitoff, size_t bitmax, FreeList* free_list) {
    if (bitmax < bitoff) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (bitmax - bitoff == 0) {
        return ZX_OK;
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
                num_bits_ -= (itr->bitlen - (bitoff - itr->bitoff));
                itr->bitlen = bitoff - itr->bitoff;
                ++itr;
                continue;
            } else {
                // '*itr' contains [bitoff, bitmax), and we need to split it.
                fbl::unique_ptr<RleBitmapElement> new_elem = AllocateElement(free_list);
                if (!new_elem) {
                    return ZX_ERR_NO_MEMORY;
                }
                ++num_elems_;
                new_elem->bitoff = bitmax;
                new_elem->bitlen = itr->bitoff + itr->bitlen - bitmax;

                elems_.insert_after(itr, fbl::move(new_elem));
                itr->bitlen = bitoff - itr->bitoff;
                num_bits_ -= (bitmax - bitoff);
                break;
            }
        } else {
            if (bitmax < itr->bitoff + itr->bitlen) {
                // 'elem' contains 'bitmax'
                num_bits_ -= (bitmax - itr->bitoff);
                itr->bitlen = itr->bitoff + itr->bitlen - bitmax;
                itr->bitoff = bitmax;
                break;
            } else {
                // [bitoff, bitmax) fully contains '*itr'.
                num_bits_ -= itr->bitlen;
                auto to_erase = itr++;
                ReleaseElement(free_list, elems_.erase(to_erase));
                --num_elems_;
            }
        }
    }

    return ZX_OK;
}

} // namespace bitmap
