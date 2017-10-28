// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>

#include "vnode.h"
#include "watcher.h"

namespace fs {

// A pseudo-directory is a directory-like object whose entries are constructed
// by a program at runtime.  The client can lookup, enumerate, and watch these
// directory entries but it cannot create, remove, or rename them.
//
// This class is designed to allow programs to publish a relatively small number
// of entries (up to a few dozen) such as services, file-system roots,
// debugging pseudo-files, or other vnodes.  It is not suitable for very large
// directories (hundreds of entries).
//
// This class is thread-safe.
class PseudoDir : public Vnode {
public:
    // Creates a directory which is initially empty.
    PseudoDir();

    // Destroys the directory and releases the nodes it contains.
    ~PseudoDir() override;

    // Adds a directory entry associating the given |name| with |vn|.
    // It is ok to add the same Vnode multiple times with different names.
    //
    // Returns |ZX_OK| on success.
    // Returns |ZX_ERR_ALREADY_EXISTS| if there is already a node with the given name.
    zx_status_t AddEntry(fbl::String name, fbl::RefPtr<fs::Vnode> vn);

    // Removes a directory entry with the given |name|.
    //
    // Returns |ZX_OK| on success.
    // Returns |ZX_ERR_NOT_FOUND| if there is no node with the given name.
    zx_status_t RemoveEntry(fbl::StringPiece name);

    // Removes all directory entries.
    void RemoveAllEntries();

    // |Vnode| implementation:
    zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;
    zx_status_t Getattr(vnattr_t* a) final;
    zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
    void Notify(fbl::StringPiece name, unsigned event) final;
    zx_status_t WatchDir(Vfs* vfs, const vfs_watch_dir_t* cmd) final;
    zx_status_t Readdir(vdircookie_t* cookie, void* dirents, size_t len, size_t* out_actual) final;

private:
    static constexpr uint64_t kDotId = 1u;

    class Entry : public fbl::DoublyLinkedListable<fbl::unique_ptr<Entry>> {
    public:
        Entry(uint64_t id, fbl::String name, fbl::RefPtr<fs::Vnode> node);
        ~Entry();

        uint64_t id() const { return id_; }
        const fbl::String& name() const { return name_; }
        const fbl::RefPtr<fs::Vnode>& node() const { return node_; }

    private:
        uint64_t const id_;
        fbl::String name_;
        fbl::RefPtr<fs::Vnode> node_;
    };
    using EntryList = fbl::DoublyLinkedList<fbl::unique_ptr<Entry>>;

    fbl::Mutex mutex_;

    uint64_t next_node_id_ __TA_GUARDED(mutex_) = kDotId + 1;
    EntryList entries_ __TA_GUARDED(mutex_);

    fs::WatcherContainer watcher_; // note: uses its own internal mutex

    DISALLOW_COPY_ASSIGN_AND_MOVE(PseudoDir);
};

} // namespace fs
