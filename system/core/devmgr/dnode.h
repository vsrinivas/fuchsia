// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>
#include <string.h>

#include <fs/vfs.h>
#include <mxio/vfs.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

#include "memfs-private.h"

namespace memfs {

constexpr size_t kDnodeNameMax = NAME_MAX;
static_assert(NAME_MAX == 255, "NAME_MAX must be 255");

// Assert that kDnodeNameMax can be used as a bitmask
static_assert(((kDnodeNameMax + 1) & kDnodeNameMax) == 0,
              "Expected kDnodeNameMax to be one less than a power of two");

class Dnode : public mxtl::DoublyLinkedListable<mxtl::RefPtr<Dnode>>,
              public mxtl::RefCounted<Dnode> {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Dnode);

    // Allocates a dnode, attached to a vnode
    static mxtl::RefPtr<Dnode> Create(const char* name, size_t len, VnodeMemfs* vn);

    // Takes a parent-less node and makes it a child of the parent node.
    //
    // Increments child link count by one.
    // If the child is a directory, increments the parent link count by one.
    static void AddChild(mxtl::RefPtr<Dnode> parent, mxtl::RefPtr<Dnode> child);

    // Removes a dnode from its parent (if dnode has a parent)
    // Decrements parent link count by one.
    void RemoveFromParent();

    // Detaches a dnode from its parent / vnode.
    // Decrements dn->vnode link count by one (if it exists).
    void Detach();

    // Look up the child dnode (within a parent directory) by name.
    // Returns NO_ERROR if the child is found.
    //
    // If the looked up child is the current node, "out" is nullptr, and
    // NO_ERROR is still returned.
    // If "out" is provided as "nullptr", the returned status appears the
    // same, but the "out" argument is not touched.
    mx_status_t Lookup(const char* name, size_t len, mxtl::RefPtr<Dnode>* out) const;

    // Acquire a pointer to the vnode underneath this dnode.
    // Acquires a reference to the underlying vnode.
    VnodeMemfs* AcquireVnode() const;

    // Returns NO_ERROR if the dnode may be unlinked
    mx_status_t CanUnlink() const;

    // Read dirents (up to len bytes worth) into data.
    // ReaddirStart reads the canned "." and ".." entries that should appear
    // at the beginning of a directory.
    // On success, return the number of bytes read.
    static mx_status_t ReaddirStart(void* cookie, void* data, size_t len);
    mx_status_t Readdir(void* cookie, void* data, size_t len) const;

    // Answers the question: "Is dn a subdirectory of this?"
    bool IsSubdirectory(mxtl::RefPtr<Dnode> dn) const;

    // Functions to take / steal the allocated dnode name.
    mxtl::unique_ptr<char[]> TakeName();
    void PutName(mxtl::unique_ptr<char[]> name, size_t len);

    bool IsDirectory() const { return vnode_->IsDirectory(); }

private:
    Dnode(VnodeMemfs* vn, mxtl::unique_ptr<char[]> name, uint32_t flags);

    size_t NameLen() const;
    bool NameMatch(const char* name, size_t len) const;

    VnodeMemfs* vnode_;
    mxtl::RefPtr<Dnode> parent_;
    mxtl::DoublyLinkedList<mxtl::RefPtr<Dnode>> children_;
    uint32_t flags_;
    mxtl::unique_ptr<char[]> name_;
};

} // namespace memfs
