// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/string.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <fs/watcher.h>
#include <zircon/types.h>
#include <zx/channel.h>

namespace svcfs {

class ServiceProvider {
public:
    virtual void Connect(fbl::StringPiece name, zx::channel channel) = 0;

protected:
    virtual ~ServiceProvider();
};

using Vnode = fs::Vnode;

class VnodeSvc : public Vnode {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VnodeSvc);
    using NodeState = fbl::DoublyLinkedListNodeState<fbl::RefPtr<VnodeSvc>>;

    struct TypeChildTraits {
        static NodeState& node_state(VnodeSvc& vn) {
            return vn.type_child_state_;
        }
    };

    VnodeSvc(uint64_t node_id,
             fbl::String name,
             ServiceProvider* provider);
    ~VnodeSvc() override;

    zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;
    zx_status_t Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) final;

    uint64_t node_id() const { return node_id_; }
    const fbl::String& name() const { return name_; }

    bool NameMatch(fbl::StringPiece name) const;
    void ClearProvider();

private:
    NodeState type_child_state_;

    // If non-zero, this vnode is a persistent child of a |VnodeDir|. Otherwise,
    // if zero, this vnode is a temporary result of a |Lookup| and supports
    // exactly one |Serve| operation.
    uint64_t node_id_;

    fbl::String name_;
    ServiceProvider* provider_;
};

class VnodeDir : public Vnode {
public:
    explicit VnodeDir();
    ~VnodeDir() override;

    zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;
    zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
    zx_status_t Getattr(vnattr_t* a) final;

    void Notify(fbl::StringPiece name, unsigned event) final;
    zx_status_t WatchDir(fs::Vfs* vfs, const vfs_watch_dir_t* cmd) final;

    zx_status_t Readdir(fs::vdircookie_t* cookie, void* dirents, size_t len) final;

    bool AddService(fbl::StringPiece name, ServiceProvider* provider);
    bool RemoveService(fbl::StringPiece name);

    void RemoveAllServices();

private:
    using ServiceList = fbl::DoublyLinkedList<fbl::RefPtr<VnodeSvc>, VnodeSvc::TypeChildTraits>;

    // Starts at 3 because . has ID one and .. has ID two.
    uint64_t next_node_id_;
    ServiceList services_;
    fs::WatcherContainer watcher_;
};

// Similar to VnodeDir, but doesn't support enumeration or watching.
class VnodeProviderDir : public Vnode {
public:
    explicit VnodeProviderDir();
    ~VnodeProviderDir() override;

    zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;
    zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
    zx_status_t Getattr(vnattr_t* a) final;

    // Set the service provider to null to prevent further requests.
    void SetServiceProvider(ServiceProvider* provider);

private:
    ServiceProvider* provider_;
};

} // namespace svcfs
