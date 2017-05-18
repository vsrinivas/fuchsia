// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fs/dispatcher.h>
#include <fs/vfs.h>
#include <magenta/types.h>
#include <mx/channel.h>
#include <mxtl/array.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_ptr.h>

namespace svcfs {

class ServiceProvider {
public:
    virtual void Connect(const char* name, size_t len, mx::channel channel) = 0;

protected:
    virtual ~ServiceProvider();
};

class Vnode : public fs::Vnode {
public:
    fs::Dispatcher* GetDispatcher() final;

    ~Vnode() override;

protected:
    explicit Vnode(fs::Dispatcher* dispatcher);

    fs::Dispatcher* dispatcher_;
};

class VnodeSvc : public Vnode {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(VnodeSvc);
    using NodeState = mxtl::DoublyLinkedListNodeState<mxtl::RefPtr<VnodeSvc>>;

    struct TypeChildTraits {
        static NodeState& node_state(VnodeSvc& vn) {
            return vn.type_child_state_;
        }
    };

    VnodeSvc(fs::Dispatcher* dispatcher,
             uint64_t node_id,
             mxtl::Array<char> name,
             ServiceProvider* provider);
    ~VnodeSvc() override;

    mx_status_t Open(uint32_t flags) final;
    mx_status_t Serve(mx_handle_t h, uint32_t flags) final;

    uint64_t node_id() const { return node_id_; }
    const mxtl::Array<char>& name() const { return name_; }

    bool NameMatch(const char* name, size_t len) const;
    void ClearProvider();

private:
    NodeState type_child_state_;

    // If non-zero, this vnode is a persistent child of a |VnodeDir|. Otherwise,
    // if zero, this vnode is a temporary result of a |Lookup| and supports
    // exactly one |Serve| operation.
    uint64_t node_id_;

    mxtl::Array<char> name_;
    ServiceProvider* provider_;
};

class VnodeDir : public Vnode {
public:
    explicit VnodeDir(fs::Dispatcher* dispatcher);
    ~VnodeDir() override;

    mx_status_t Open(uint32_t flags) final;
    mx_status_t Lookup(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len) final;
    mx_status_t Getattr(vnattr_t* a) final;

    void NotifyAdd(const char* name, size_t len) final;
    mx_status_t WatchDir(mx_handle_t* out) final;

    mx_status_t Readdir(void* cookie, void* dirents, size_t len) final;

    bool AddService(const char* name, size_t len, ServiceProvider* provider);
    void RemoveAllServices();

private:
    using ServiceList = mxtl::DoublyLinkedList<mxtl::RefPtr<VnodeSvc>, VnodeSvc::TypeChildTraits>;

    // Starts at 3 because . has ID one and .. has ID two.
    uint64_t next_node_id_;
    ServiceList services_;
    fs::WatcherContainer watcher_;
};

// Similar to VnodeDir, but doesn't support enumeration or watching.
class VnodeProviderDir : public Vnode {
public:
    explicit VnodeProviderDir(fs::Dispatcher* dispatcher);
    ~VnodeProviderDir() override;

    mx_status_t Open(uint32_t flags) final;
    mx_status_t Lookup(mxtl::RefPtr<fs::Vnode>* out, const char* name, size_t len) final;
    mx_status_t Getattr(vnattr_t* a) final;

    // Set the service provider to null to prevent further requests.
    void SetServiceProvider(ServiceProvider* provider);

private:
    ServiceProvider* provider_;
};

} // namespace svcfs
