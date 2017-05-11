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

namespace memfs {

class VnodeMemfs;

constexpr size_t kDnodeNameMax = NAME_MAX;
static_assert(NAME_MAX == 255, "NAME_MAX must be 255");

// Assert that kDnodeNameMax can be used as a bitmask
static_assert(((kDnodeNameMax + 1) & kDnodeNameMax) == 0,
              "Expected kDnodeNameMax to be one less than a power of two");

class Dnode : public mxtl::RefCounted<Dnode> {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Dnode);
    using NodeState = mxtl::DoublyLinkedListNodeState<mxtl::RefPtr<Dnode>>;

    // ChildTraits is the state used for a Dnode to appear as the child
    // of another dnode.
    struct TypeChildTraits { static NodeState& node_state(Dnode& dn) { return dn.type_child_state_; }};
    // DeviceTraits it the state used by devices to effectively create
    // multiple hard links to a single device vnode. This is used
    // extensively by the device manager to make the "same" device
    // vnode appear in multiple locations within "/dev".
    struct TypeDeviceTraits { static NodeState& node_state(Dnode& dn) { return dn.type_device_state_; }};

    using ChildList = mxtl::DoublyLinkedList<mxtl::RefPtr<Dnode>, Dnode::TypeChildTraits>;
    using DeviceList = mxtl::DoublyLinkedList<mxtl::RefPtr<Dnode>, Dnode::TypeDeviceTraits>;

    // Allocates a dnode, attached to a vnode
    static mxtl::RefPtr<Dnode> Create(const char* name, size_t len, mxtl::RefPtr<VnodeMemfs> vn);

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

    bool HasChildren() const { return !children_.is_empty(); }

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
    mxtl::RefPtr<VnodeMemfs> AcquireVnode() const;

    // Returns NO_ERROR if the dnode may be unlinked
    mx_status_t CanUnlink() const;

    // Read dirents (up to len bytes worth) into data.
    // ReaddirStart reads the canned "." and ".." entries that should appear
    // at the beginning of a directory.
    // On success, return the number of bytes read.
    static mx_status_t ReaddirStart(fs::DirentFiller* df, void* cookie);
    void Readdir(fs::DirentFiller* df, void* cookie) const;

    // Answers the question: "Is dn a subdirectory of this?"
    bool IsSubdirectory(mxtl::RefPtr<Dnode> dn) const;

    // Functions to take / steal the allocated dnode name.
    mxtl::unique_ptr<char[]> TakeName();
    void PutName(mxtl::unique_ptr<char[]> name, size_t len);

    bool IsDirectory() const;

private:
    friend struct TypeChildTraits;
    friend struct TypeDeviceTraits;

    Dnode(mxtl::RefPtr<VnodeMemfs> vn, mxtl::unique_ptr<char[]> name, uint32_t flags);

    size_t NameLen() const;
    bool NameMatch(const char* name, size_t len) const;

    NodeState type_child_state_;
    NodeState type_device_state_;
    mxtl::RefPtr<VnodeMemfs> vnode_;
    mxtl::RefPtr<Dnode> parent_;
    // Used to impose an absolute order on dnodes within a directory.
    size_t ordering_token_;
    ChildList children_;
    uint32_t flags_;
    mxtl::unique_ptr<char[]> name_;
};

} // namespace memfs
