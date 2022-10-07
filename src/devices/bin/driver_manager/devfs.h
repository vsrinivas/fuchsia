// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_H_

#include <fidl/fuchsia.device.fs/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async/dispatcher.h>

#include <variant>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

class Devfs;
class Device;
class PseudoDir;

class Devnode
    : public fbl::DoublyLinkedListable<Devnode*, fbl::NodeOptions::AllowRemoveFromContainer> {
 public:
  using ExportOptions = fuchsia_device_fs::wire::ExportOptions;
  struct NoRemote {
    mutable ExportOptions export_options;
  };
  struct Service {
    fidl::ClientEnd<fuchsia_io::Directory> remote;
    std::string path;
    mutable ExportOptions export_options;
  };

  using Target = std::variant<NoRemote, Service, std::reference_wrapper<Device>>;

  // Constructs a root node.
  Devnode(Devfs& devfs, Device* device);

  // `parent` must outlive `this`.
  Devnode(Devfs& devfs, PseudoDir& parent, Target target, fbl::String name);

  ~Devnode();

  Devnode(const Devnode&) = delete;
  Devnode& operator=(const Devnode&) = delete;

  Devnode(Devnode&&) = delete;
  Devnode& operator=(Devnode&&) = delete;

  zx::status<Devnode*> walk(std::string_view path);

  // Exports `service_path` from `service_dir` to `devfs_path`, under `dn`. If
  // `protocol_id` matches a known protocol, `service_path` will also be exposed
  // under a class path.
  //
  // Every Devnode that is created during the export is stored within `out`. As
  // each of these Devnodes are children of `dn`, they must live as long as `dn`.
  zx_status_t export_dir(fidl::ClientEnd<fuchsia_io::Directory> service_dir,
                         std::string_view service_path, std::string_view devfs_path,
                         uint32_t protocol_id, ExportOptions options,
                         std::vector<std::unique_ptr<Devnode>>& out);

  const Device* device() const;
  std::string_view name() const;
  PseudoDir& children() const { return node().children(); }
  ExportOptions export_options() const;
  ExportOptions* export_options();

  // Publishes the node to devfs. Asserts if called more than once.
  void publish();

  // The actual vnode implementation. This is distinct from the outer class
  // because `fs::Vnode` imposes reference-counted semantics, and we want to
  // preserve owned semantics on the outer class.
  //
  // This is exposed for use in tests.
  class VnodeImpl : public fs::Vnode {
   public:
    fs::VnodeProtocolSet GetProtocols() const final;
    zx_status_t GetAttributes(fs::VnodeAttributes* a) final;
    zx_status_t Lookup(std::string_view name, fbl::RefPtr<fs::Vnode>* out) final;
    zx_status_t WatchDir(fs::Vfs* vfs, fuchsia_io::wire::WatchMask mask, uint32_t options,
                         fidl::ServerEnd<fuchsia_io::DirectoryWatcher> watcher) final;
    zx_status_t Readdir(fs::VdirCookie* cookie, void* dirents, size_t len,
                        size_t* out_actual) final;
    zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                       fs::VnodeRepresentation* info) final;
    zx_status_t OpenNode(ValidatedOptions options, fbl::RefPtr<Vnode>* out_redirect) final;

    bool IsSkipRightsEnforcementDevfsOnlyDoNotUse() const final { return true; }

    PseudoDir& children() const { return *children_; }

    Devnode& holder_;
    const Target target_;

   private:
    friend fbl::internal::MakeRefCountedHelper<VnodeImpl>;

    VnodeImpl(Devnode& holder, Target target);

    bool IsDirectory() const;

    class RemoteNode : public fs::Vnode {
     private:
      friend fbl::internal::MakeRefCountedHelper<RemoteNode>;

      explicit RemoteNode(VnodeImpl& parent) : parent_(parent) {}

      fs::VnodeProtocolSet GetProtocols() const final;
      zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                         fs::VnodeRepresentation* info) final;

      bool IsRemote() const final;
      zx_status_t OpenRemote(fuchsia_io::OpenFlags, uint32_t, fidl::StringView,
                             fidl::ServerEnd<fuchsia_io::Node>) const final;

      VnodeImpl& parent_;
    };

    fbl::RefPtr<PseudoDir> children_ = fbl::MakeRefCounted<PseudoDir>();
    fbl::RefPtr<RemoteNode> remote_ = fbl::MakeRefCounted<RemoteNode>(*this);
  };

 private:
  VnodeImpl& node() const { return *node_; }
  const Target& target() const { return node_->target_; }

  friend class Devfs;
  friend class PseudoDir;

  Devfs& devfs_;

  fbl::RefPtr<PseudoDir> parent_;

  const fbl::RefPtr<VnodeImpl> node_;

  const std::optional<fbl::String> name_;
};

class PseudoDir : public fs::PseudoDir {
 public:
  fbl::DoublyLinkedList<Devnode*> unpublished;

 private:
  PseudoDir() : fs::PseudoDir(nullptr, false) {}
  bool IsSkipRightsEnforcementDevfsOnlyDoNotUse() const final { return true; }

  friend fbl::internal::MakeRefCountedHelper<PseudoDir>;
};

// Represents an entry in the /dev/class directory. A thin wrapper around
// `PseudoDir` that can be owned and keeps track of numbered entries
// contained within it.
class ProtoNode {
 public:
  explicit ProtoNode(fbl::String name);

  ProtoNode(const ProtoNode&) = delete;
  ProtoNode& operator=(const ProtoNode&) = delete;

  ProtoNode(ProtoNode&&) = delete;
  ProtoNode& operator=(ProtoNode&&) = delete;

  // This is used in tests only.
  std::string_view name() const { return name_; }
  PseudoDir& children() const { return *children_; }

 private:
  friend class Devfs;
  friend class Devnode;

  zx::status<fbl::String> seq_name();

  const fbl::String name_;

  uint32_t seqcount_ = 0;

  fbl::RefPtr<PseudoDir> children_ = fbl::MakeRefCounted<PseudoDir>();
};

class Devfs {
 public:
  // `root` and `device` must outlive `this`.
  Devfs(std::optional<Devnode>& root, Device* device,
        std::optional<fidl::ClientEnd<fuchsia_io::Directory>> diagnostics = {});

  zx::status<fidl::ClientEnd<fuchsia_io::Directory>> Connect(fs::FuchsiaVfs& vfs);

  zx_status_t initialize(Device& device);
  void publish(Device& device);
  void advertise_modified(Device& dev);

  // This method is exposed for testing.
  ProtoNode* proto_node(uint32_t protocol_id);

 private:
  friend class Devnode;

  Devnode& root_;

  fbl::RefPtr<PseudoDir> class_ = fbl::MakeRefCounted<PseudoDir>();

  struct ProtocolInfo {
    std::string_view name;
    uint32_t id;
    uint32_t flags;
  };

  static constexpr ProtocolInfo proto_infos[] = {
#define DDK_PROTOCOL_DEF(tag, val, name, flags) {name, val, flags},
#include <lib/ddk/protodefs.h>
  };

  std::unordered_map<uint32_t, ProtoNode> proto_info_nodes;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVFS_H_
