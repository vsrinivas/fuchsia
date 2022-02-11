// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGICAL_BUFFER_COLLECTION_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGICAL_BUFFER_COLLECTION_H_

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fidl/fuchsia.sysmem2/cpp/wire.h>
#include <inttypes.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl-async-2/fidl_struct.h>
#include <lib/fidl/llcpp/arena.h>
#include <lib/zx/channel.h>

#include <list>
#include <map>
#include <memory>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_printf.h>

#include "allocation_result.h"
#include "device.h"
#include "logging.h"
#include "node.h"
#include "node_properties.h"
#include "table_set.h"

namespace sysmem_driver {

class BufferCollectionToken;
class BufferCollection;
class MemoryAllocator;

// This class can be used to hold an inspect snapshot of one set of constraints taken from a client
// at a particular point in time.
struct ConstraintInfoSnapshot {
  inspect::Node inspect_node;
  inspect::ValueList node_constraints;
};

// TODO(dustingreen): MaybeAllocate() should sweep all related incoming channels for ZX_PEER_CLOSED
// and not attempt allocation until all channel close(es) that were pending at the time have been
// processed.  Ignoring new channel closes is fine/good.
class LogicalBufferCollection : public fbl::RefCounted<LogicalBufferCollection> {
 public:
  using Arena = fidl::Arena<>;
  using CollectionMap = std::map<BufferCollection*, fbl::RefPtr<BufferCollection>>;

  ~LogicalBufferCollection();

  static void Create(zx::channel buffer_collection_token_request, Device* parent_device);

  // |parent_device| the Device* that the calling allocator is part of.  The
  // tokens_by_koid_ for each Device is separate.  If somehow two clients were
  // to get connected to two separate sysmem device instances hosted in the
  // same devhost, those clients (intentionally) won't be able to share a
  // LogicalBufferCollection.
  //
  // |buffer_collection_token| the client end of the BufferCollectionToken
  // being turned in by the client to get a BufferCollection in exchange.
  //
  // |buffer_collection_request| the server end of a BufferCollection channel
  // to be served by the LogicalBufferCollection associated with
  // buffer_collection_token.
  static void BindSharedCollection(Device* parent_device, zx::channel buffer_collection_token,
                                   zx::channel buffer_collection_request,
                                   const ClientDebugInfo* client_debug_info);

  // ZX_OK if the token is known to the server.
  // ZX_ERR_NOT_FOUND if the token isn't known to the server.
  static zx_status_t ValidateBufferCollectionToken(Device* parent_device,
                                                   zx_koid_t token_server_koid);

  // This is used to create the initial BufferCollectionToken, and also used
  // by BufferCollectionToken::Duplicate().
  //
  // The |self| parameter exists only because LogicalBufferCollection can't
  // hold a std::weak_ptr<> to itself because that requires libc++ (the binary
  // not just the headers) which isn't available in Zircon so far.
  void CreateBufferCollectionToken(
      fbl::RefPtr<LogicalBufferCollection> self, NodeProperties* new_node_properties,
      fidl::ServerEnd<fuchsia_sysmem::BufferCollectionToken> token_request);

  void AttachLifetimeTracking(zx::eventpair server_end, uint32_t buffers_remaining);
  void SweepLifetimeTracking();

  void OnSetConstraints();

  void SetName(uint32_t priority, std::string name);
  void SetDebugTimeoutLogDeadline(int64_t deadline);

  uint64_t CreateDispensableOrdinal();

  void VLogClient(bool is_error, Location location, const NodeProperties* node_properties,
                  const char* format, va_list args) const;
  void LogClientInfo(Location location, const NodeProperties* node_properties, const char* format,
                     ...) const __PRINTFLIKE(4, 5);
  void LogClientError(Location location, const NodeProperties* node_properties, const char* format,
                      ...) const __PRINTFLIKE(4, 5);
  void VLogClientInfo(Location location, const NodeProperties* node_properties, const char* format,
                      va_list args) const;
  void VLogClientError(Location location, const NodeProperties* node_properties, const char* format,
                       va_list args) const;

  Device* parent_device() const { return parent_device_; }

  // For tests.
  std::vector<const BufferCollection*> collection_views() const;

  TableSet& table_set() { return table_set_; }

  std::optional<std::string> name() const {
    return name_ ? std::make_optional(name_->name) : std::optional<std::string>();
  }

  inspect::Node& inspect_node() { return inspect_node_; }

 private:
  friend class NodeProperties;

  enum class CheckSanitizeStage { kInitial, kNotAggregated, kAggregated };

  class Constraints {
   public:
    Constraints(const Constraints&) = delete;
    Constraints(Constraints&&) = default;
    Constraints(TableSet& table_set,
                fuchsia_sysmem2::wire::BufferCollectionConstraints&& constraints,
                const NodeProperties& node_properties)
        : buffer_collection_constraints_(table_set, std::move(constraints)),
          node_properties_(node_properties) {}

    const fuchsia_sysmem2::wire::BufferCollectionConstraints& constraints() const {
      return *buffer_collection_constraints_;
    }
    fuchsia_sysmem2::wire::BufferCollectionConstraints& mutate_constraints() {
      return buffer_collection_constraints_.mutate();
    }

    const ClientDebugInfo& client_debug_info() const {
      return node_properties_.client_debug_info();
    }
    const NodeProperties& node_properties() const { return node_properties_; }

   private:
    TableHolder<fuchsia_sysmem2::wire::BufferCollectionConstraints> buffer_collection_constraints_;
    const NodeProperties& node_properties_;
  };

  using ConstraintsList = std::list<Constraints>;

  struct CollectionName {
    uint32_t priority{};
    std::string name;
  };

  LogicalBufferCollection(Device* parent_device);

  // Will log an error, and then FailRoot().
  void LogAndFailRootNode(Location location, zx_status_t epitaph, const char* format, ...)
      __PRINTFLIKE(4, 5);
  // This fails the entire LogicalBufferCollection, by failing the root Node, which propagates that
  // failure to the entire Node tree, and also results in zero remaining Node(s).  Then once all
  // outstanding VMOs have been closed, the LogicalBufferCollection is deleted.
  //
  // This cleans out a lot of state that's unnecessary after a failure.
  void FailRootNode(zx_status_t epitaph);

  NodeProperties* FindTreeToFail(NodeProperties* failing_node);

  // Fails the tree rooted at tree_to_fail.  The tree_to_fail is allowed to be the root_, or it can
  // be a sub-tree. All the Node(s) from tree_to_fail down are Fail()ed.  Any Node(s) that still
  // have channels open will send epitaph just before the Node('s) channel closes.  All Node(s) from
  // tree_to_fail downward are also removed from the tree. If tree_to_fail is the root, then when
  // all child VMOs have been closed, the LogicalBufferCollection will be deleted.
  //
  // Node(s) are Fail()ed and removed from the tree in child-first order.
  //
  // These two are only appropriate to use if it's already known which node should fail.  If it's
  // not yet known which node should fail, call FindTreeToFail() first, or use LogAndFailNode() /
  // FailNode().
  void LogAndFailDownFrom(Location location, NodeProperties* tree_to_fail, zx_status_t epitaph,
                          const char* format, ...) __PRINTFLIKE(5, 6);
  void FailDownFrom(NodeProperties* tree_to_fail, zx_status_t epitaph);

  // Find the root-most node of member_node's failure domain, and fail that whole failure domain and
  // any of its child failure domains.
  void LogAndFailNode(Location location, NodeProperties* member_node, zx_status_t epitaph,
                      const char* format, ...) __PRINTFLIKE(5, 6);
  void FailNode(NodeProperties* member_node, zx_status_t epitaph);

  static void LogInfo(Location location, const char* format, ...);
  static void LogErrorStatic(Location location, const ClientDebugInfo* client_debug_info,
                             const char* format, ...) __PRINTFLIKE(3, 4);

  void LogError(Location location, const char* format, ...) const __PRINTFLIKE(3, 4);
  void VLogError(Location location, const char* format, va_list args) const;

  // The caller must keep "this" alive.  We require this of the caller since the caller is fairly
  // likely to want to keep "this" alive longer than MaybeAllocate() could anyway.
  void MaybeAllocate();

  void TryAllocate(std::vector<NodeProperties*> nodes);

  void TryLateLogicalAllocation(std::vector<NodeProperties*> nodes);

  void InitializeConstraintSnapshots(const ConstraintsList& constraints_list);

  void SetFailedAllocationResult(zx_status_t status);

  void SetAllocationResult(std::vector<NodeProperties*> nodes,
                           fuchsia_sysmem2::wire::BufferCollectionInfo&& info);

  void SendAllocationResult(std::vector<NodeProperties*> nodes);

  void SetFailedLateLogicalAllocationResult(NodeProperties* tree, zx_status_t status_param);

  void SetSucceededLateLogicalAllocationResult(std::vector<NodeProperties*> nodes);

  void BindSharedCollectionInternal(BufferCollectionToken* token,
                                    zx::channel buffer_collection_request);

  fpromise::result<fuchsia_sysmem2::wire::BufferCollectionConstraints, void> CombineConstraints(
      ConstraintsList* constraints_list);

  bool CheckSanitizeBufferCollectionConstraints(
      CheckSanitizeStage stage, fuchsia_sysmem2::wire::BufferCollectionConstraints& constraints);

  bool CheckSanitizeBufferUsage(CheckSanitizeStage stage,
                                fuchsia_sysmem2::wire::BufferUsage& buffer_usage);

  bool CheckSanitizeBufferMemoryConstraints(
      CheckSanitizeStage stage, const fuchsia_sysmem2::wire::BufferUsage& buffer_usage,
      fuchsia_sysmem2::wire::BufferMemoryConstraints& constraints);

  bool CheckSanitizeImageFormatConstraints(
      CheckSanitizeStage stage, fuchsia_sysmem2::wire::ImageFormatConstraints& constraints);

  bool AccumulateConstraintBufferCollection(fuchsia_sysmem2::wire::BufferCollectionConstraints* acc,
                                            fuchsia_sysmem2::wire::BufferCollectionConstraints c);

  bool AccumulateConstraintsBufferUsage(fuchsia_sysmem2::wire::BufferUsage* acc,
                                        fuchsia_sysmem2::wire::BufferUsage c);

  bool AccumulateConstraintHeapPermitted(fidl::VectorView<fuchsia_sysmem2::wire::HeapType>* acc,
                                         fidl::VectorView<fuchsia_sysmem2::wire::HeapType> c);

  bool AccumulateConstraintBufferMemory(fuchsia_sysmem2::wire::BufferMemoryConstraints* acc,
                                        fuchsia_sysmem2::wire::BufferMemoryConstraints c);

  bool AccumulateConstraintImageFormats(
      fidl::VectorView<fuchsia_sysmem2::wire::ImageFormatConstraints>* acc,
      fidl::VectorView<fuchsia_sysmem2::wire::ImageFormatConstraints> c);

  bool AccumulateConstraintImageFormat(fuchsia_sysmem2::wire::ImageFormatConstraints* acc,
                                       fuchsia_sysmem2::wire::ImageFormatConstraints c);

  bool AccumulateConstraintColorSpaces(fidl::VectorView<fuchsia_sysmem2::wire::ColorSpace>* acc,
                                       fidl::VectorView<fuchsia_sysmem2::wire::ColorSpace> c);

  size_t InitialCapacityOrZero(CheckSanitizeStage stage, size_t initial_capacity);

  bool IsColorSpaceEqual(const fuchsia_sysmem2::wire::ColorSpace& a,
                         const fuchsia_sysmem2::wire::ColorSpace& b);

  fpromise::result<fuchsia_sysmem2::wire::BufferCollectionInfo, zx_status_t>
  GenerateUnpopulatedBufferCollectionInfo(
      const fuchsia_sysmem2::wire::BufferCollectionConstraints& constraints);

  fpromise::result<fuchsia_sysmem2::wire::BufferCollectionInfo, zx_status_t> Allocate(
      const fuchsia_sysmem2::wire::BufferCollectionConstraints& constraints,
      fuchsia_sysmem2::wire::BufferCollectionInfo* buffer_collection_info);

  fpromise::result<zx::vmo> AllocateVmo(MemoryAllocator* allocator,
                                        const fuchsia_sysmem2::wire::SingleBufferSettings& settings,
                                        uint32_t index);

  int32_t CompareImageFormatConstraintsTieBreaker(
      const fuchsia_sysmem2::wire::ImageFormatConstraints& a,
      const fuchsia_sysmem2::wire::ImageFormatConstraints& b);

  int32_t CompareImageFormatConstraintsByIndex(
      const fuchsia_sysmem2::wire::BufferCollectionConstraints& constraints, uint32_t index_a,
      uint32_t index_b);

  void CreationTimedOut(async_dispatcher_t* dispatcher, async::TaskBase* task, zx_status_t status);

  AllocationResult allocation_result();

  void LogBufferCollectionInfoDiffs(const fuchsia_sysmem2::wire::BufferCollectionInfo& o,
                                    const fuchsia_sysmem2::wire::BufferCollectionInfo& n);

  // To be called only by CombineConstraints().
  bool IsMinBufferSizeSpecifiedByAnyParticipant(const ConstraintsList& constraints_list);

  // This is the root and any sub-trees created via SetDispensable() or AttachToken().
  std::vector<NodeProperties*> FailureDomainSubtrees();

  // A Node is a pruned sub-tree eligible for logical allocation if it is not yet logically
  // allocated, and does not have a non-logically-allocated direct parent.
  //
  // Such sub-trees will always have a root-most Node that is either the root or an
  // ErrorPropagationMode kDoNotPropagate Node.
  //
  // The returned pruned sub-trees may not be ready for logical allocation (may not have all its
  // constraints yet), nor do they necessarily have constraints that are satisfiable.
  std::vector<NodeProperties*> PrunedSubtreesEligibleForLogicalAllocation();

  // The granularity of logical allocation is a sub-tree pruned of any ErrorPropagationMode
  // kDoNotPropagate Node(s) other than the sub-tree's root-most node.
  //
  // The root-most Node of a pruned subtree can be the root, or it can be a Node that has
  // ErrorPropagationMode kDoNotPropagate (implying that AttachToken() was used to create the
  // non-root Node).
  //
  // The pruning is the same for both the root and a non-root Node so that the locally-observable
  // (by a participant) logical allocation granularity is the same regardless of whether the logical
  // allocation granule has the root as its root-most Node, or has an ErrorPropagationMode
  // kDoNotPropagate Node as its root-most Node.
  std::vector<NodeProperties*> NodesOfPrunedSubtreeEligibleForLogicalAllocation(
      NodeProperties& subtree);

  void LogTreeForDebugOnly(NodeProperties* node);

  // For NodeProperties:
  void AddCountsForNode(const Node& node);
  void RemoveCountsForNode(const Node& node);
  void AdjustRelevantNodeCounts(const Node& node, fit::function<void(uint32_t& count)> visitor);
  void DeleteRoot();

  // Diff printing.

#if defined(PRINT_DIFF)
#error "Let's pick a different name for PRINT_DIFF"
#endif
  // If LLCPP had generic field objects, we wouldn't need this macro.  We #undef the macro name
  // after we're done using it so it doesn't escape this header.
#define PRINT_DIFF(child_field_name)                                                              \
  do {                                                                                            \
    const std::string& parent_field_name = field_name;                                            \
    if (o.has_##child_field_name() != n.has_##child_field_name()) {                               \
      LogError(FROM_HERE,                                                                         \
               "o%s.has_" #child_field_name "(): %d n%s.has_" #child_field_name "(): %d",         \
               parent_field_name.c_str(), o.has_##child_field_name(), parent_field_name.c_str(),  \
               n.has_##child_field_name());                                                       \
    } else if (o.has_##child_field_name()) {                                                      \
      std::string field_name =                                                                    \
          fbl::StringPrintf("%s.%s()", parent_field_name.c_str(), #child_field_name).c_str();     \
      DiffPrinter<std::remove_const_t<std::remove_reference_t<decltype(o.child_field_name())>>>:: \
          PrintDiff(*this, field_name, o.child_field_name(), n.child_field_name());               \
    }                                                                                             \
  } while (false)

  template <typename FieldType, typename Enable = void>
  struct DiffPrinter {
    static void PrintDiff(const LogicalBufferCollection& buffer_collection,
                          const std::string& field_name, const FieldType& o, const FieldType& n);
  };
  template <typename ElementType>
  struct DiffPrinter<fidl::VectorView<ElementType>,
                     std::enable_if_t<fidl::IsVectorView<fidl::VectorView<ElementType>>::value>> {
    static void PrintDiff(const LogicalBufferCollection& buffer_collection,
                          const std::string& field_name, const fidl::VectorView<ElementType>& o,
                          const fidl::VectorView<ElementType>& n) {
      if (o.count() != n.count()) {
        buffer_collection.LogError(FROM_HERE, "o%s.count(): %" PRIu64 " n%s.count(): %" PRIu64,
                                   field_name.c_str(), o.count(), field_name.c_str(), n.count());
      }
      for (uint32_t i = 0; i < std::max(o.count(), n.count()); ++i) {
        std::string new_field_name = fbl::StringPrintf("%s[%u]", field_name.c_str(), i).c_str();
        const ElementType& blank = ElementType();
        const ElementType& o_element = i < o.count() ? o[i] : blank;
        const ElementType& n_element = i < n.count() ? n[i] : blank;
        DiffPrinter<ElementType>::PrintDiff(buffer_collection, new_field_name, o_element,
                                            n_element);
      }
    }
  };
  template <typename TableType>
  struct DiffPrinter<TableType, std::enable_if_t<fidl::IsTable<TableType>::value>> {
    static void PrintDiff(const LogicalBufferCollection& buffer_collection,
                          const std::string& field_name, const TableType& o, const TableType& n) {
      buffer_collection.LogTableDiffs<TableType>(field_name, o, n);
    }
  };
  template <>
  struct DiffPrinter<bool, void> {
    static void PrintDiff(const LogicalBufferCollection& buffer_collection,
                          const std::string& field_name, const bool& o, const bool& n) {
      if (o != n) {
        buffer_collection.LogError(FROM_HERE, "o%s: %d n%s: %d", field_name.c_str(), o,
                                   field_name.c_str(), n);
      }
    }
  };
  template <>
  struct DiffPrinter<uint32_t, void> {
    static void PrintDiff(const LogicalBufferCollection& buffer_collection,
                          const std::string& field_name, const uint32_t& o, const uint32_t& n) {
      if (o != n) {
        buffer_collection.LogError(FROM_HERE, "o%s: %u n%s: %u", field_name.c_str(), o,
                                   field_name.c_str(), n);
      }
    }
  };
  template <typename EnumType>
  struct DiffPrinter<EnumType, std::enable_if_t<std::is_enum_v<EnumType>>> {
    static void PrintDiff(const LogicalBufferCollection& buffer_collection,
                          const std::string& field_name, const EnumType& o, const EnumType& n) {
      using UnderlyingType = std::underlying_type_t<EnumType>;
      const UnderlyingType o_underlying = static_cast<UnderlyingType>(o);
      const UnderlyingType n_underlying = static_cast<UnderlyingType>(n);
      DiffPrinter<UnderlyingType>::PrintDiff(buffer_collection, field_name, o_underlying,
                                             n_underlying);
    }
  };
  template <>
  struct DiffPrinter<uint64_t, void> {
    static void PrintDiff(const LogicalBufferCollection& buffer_collection,
                          const std::string& field_name, const uint64_t& o, const uint64_t& n) {
      if (o != n) {
        buffer_collection.LogError(FROM_HERE, "o%s: %" PRIu64 " n%s: %" PRIu64, field_name.c_str(),
                                   o, field_name.c_str(), n);
      }
    }
  };
  template <>
  struct DiffPrinter<zx::vmo, void> {
    static void PrintDiff(const LogicalBufferCollection& buffer_collection,
                          const std::string& field_name, const zx::vmo& o, const zx::vmo& n) {
      // We don't expect to call the zx::vmo variant since !has_vmo() and !has_aux_vmo(), but if we
      // do get here, complain + print the values regardless of what the values are or whether they
      // differ.
      buffer_collection.LogError(FROM_HERE,
                                 "Why did we call zx::vmo PrintDiff? --- o%s: %u n%s: %u",
                                 field_name.c_str(), o.get(), field_name.c_str(), n.get());
    }
  };

  template <typename Table>
  void LogTableDiffs(const std::string& field_name, const Table& o, const Table& n) const;

  template <>
  void LogTableDiffs<fuchsia_sysmem2::wire::BufferMemorySettings>(
      const std::string& field_name, const fuchsia_sysmem2::wire::BufferMemorySettings& o,
      const fuchsia_sysmem2::wire::BufferMemorySettings& n) const {
    PRINT_DIFF(size_bytes);
    PRINT_DIFF(is_physically_contiguous);
    PRINT_DIFF(is_secure);
    PRINT_DIFF(coherency_domain);
    PRINT_DIFF(heap);
  }

  template <>
  void LogTableDiffs<fuchsia_sysmem2::wire::PixelFormat>(
      const std::string& field_name, const fuchsia_sysmem2::wire::PixelFormat& o,
      const fuchsia_sysmem2::wire::PixelFormat& n) const {
    PRINT_DIFF(type);
    PRINT_DIFF(format_modifier_value);
  }

  template <>
  void LogTableDiffs<fuchsia_sysmem2::wire::ColorSpace>(
      const std::string& field_name, const fuchsia_sysmem2::wire::ColorSpace& o,
      const fuchsia_sysmem2::wire::ColorSpace& n) const {
    PRINT_DIFF(type);
  }

  template <>
  void LogTableDiffs<fuchsia_sysmem2::wire::ImageFormatConstraints>(
      const std::string& field_name, const fuchsia_sysmem2::wire::ImageFormatConstraints& o,
      const fuchsia_sysmem2::wire::ImageFormatConstraints& n) const {
    PRINT_DIFF(pixel_format);
    PRINT_DIFF(color_spaces);
    PRINT_DIFF(min_coded_width);
    PRINT_DIFF(max_coded_width);
    PRINT_DIFF(min_coded_height);
    PRINT_DIFF(max_coded_height);
    PRINT_DIFF(min_bytes_per_row);
    PRINT_DIFF(max_bytes_per_row);
    PRINT_DIFF(max_coded_width_times_coded_height);
    PRINT_DIFF(coded_width_divisor);
    PRINT_DIFF(coded_height_divisor);
    PRINT_DIFF(bytes_per_row_divisor);
    PRINT_DIFF(start_offset_divisor);
    PRINT_DIFF(display_width_divisor);
    PRINT_DIFF(display_height_divisor);
    PRINT_DIFF(required_min_coded_width);
    PRINT_DIFF(required_max_coded_width);
    PRINT_DIFF(required_min_coded_height);
    PRINT_DIFF(required_max_coded_height);
    PRINT_DIFF(required_min_bytes_per_row);
    PRINT_DIFF(required_max_bytes_per_row);
  }

  template <>
  void LogTableDiffs<fuchsia_sysmem2::wire::SingleBufferSettings>(
      const std::string& field_name, const fuchsia_sysmem2::wire::SingleBufferSettings& o,
      const fuchsia_sysmem2::wire::SingleBufferSettings& n) const {
    PRINT_DIFF(buffer_settings);
    PRINT_DIFF(image_format_constraints);
  }

  template <>
  void LogTableDiffs<fuchsia_sysmem2::wire::VmoBuffer>(
      const std::string& field_name, const fuchsia_sysmem2::wire::VmoBuffer& o,
      const fuchsia_sysmem2::wire::VmoBuffer& n) const {
    PRINT_DIFF(vmo);
    PRINT_DIFF(vmo_usable_start);
    PRINT_DIFF(aux_vmo);
  }

  template <>
  void LogTableDiffs<fuchsia_sysmem2::wire::BufferCollectionInfo>(
      const std::string& field_name, const fuchsia_sysmem2::wire::BufferCollectionInfo& o,
      const fuchsia_sysmem2::wire::BufferCollectionInfo& n) const {
    PRINT_DIFF(settings);
    PRINT_DIFF(buffers);
  }
#undef PRINT_DIFF

  void LogDiffsBufferCollectionInfo(const fuchsia_sysmem2::wire::BufferCollectionInfo& o,
                                    const fuchsia_sysmem2::wire::BufferCollectionInfo& n) const {
    LOG(WARNING, "LogDiffsBufferCollectionInfo()");
    LogTableDiffs<fuchsia_sysmem2::wire::BufferCollectionInfo>("", o, n);
  }

  void LogConstraints(Location location, NodeProperties* node_properties,
                      const fuchsia_sysmem2::wire::BufferCollectionConstraints& constraints) const;

  Device* parent_device_ = nullptr;

  // We occasionally swap out the allocator for a fresh one, to avoid the possibility of churn
  // leading to excessive un-used memory allocation in the allocator.  This is accomplished via
  // TableHolder and TableSet.
  TableSet table_set_;

  // This owns the current tree of BufferCollectionToken, BufferCollection, OrphanedNode.  The
  // Location in the tree is determined by creation path.  Child Node(s) are children because they
  // were created via their parent node.  The root node is created by CreateSharedCollection() which
  // also creates the LogicalBufferCollection.
  //
  // We use shared_ptr<> to manage NodeProperties only so that Node can have a std::weak_ptr<> back
  // to NodeProperties instead of a raw pointer, since the Node can last longer than NodeProperties,
  // despite NodeProperties being the primary non-transient owner of Node.  This way we avoid using
  // a raw pointer from Node to NodeProperties.
  //
  // The tree at root_ is the only non-transient ownership of NodeProperties.  Transient ownership
  // lasts only during a portion of a single dispatch on parent_device_->dispatcher().
  std::shared_ptr<NodeProperties> root_;

  std::vector<ConstraintInfoSnapshot> constraints_at_allocation_;

  bool is_allocate_attempted_ = false;

  // Iff true, initial allocation has been attempted and has succeeded or
  // failed.  Both allocation_result_status_ and allocation_result_info_ are
  // not meaningful until has_allocation_result_ is true.
  bool has_allocation_result_ = false;
  std::optional<TableHolder<fuchsia_sysmem2::wire::BufferCollectionInfo>>
      buffer_collection_info_before_population_;
  std::optional<fidl::unstable::OwnedEncodedMessage<fuchsia_sysmem2::wire::BufferCollectionInfo>>
      linearized_buffer_collection_info_before_population_;
  zx_status_t allocation_result_status_ = ZX_OK;
  std::optional<TableHolder<fuchsia_sysmem2::wire::BufferCollectionInfo>> allocation_result_info_;

  MemoryAllocator* memory_allocator_ = nullptr;
  std::optional<CollectionName> name_;

  // 0 means not dispensable.  Non-zero means dispensable, with each value being a group of
  // BufferCollectionToken(s) / BufferCollection(s) that were all created from a single
  // BufferCollectionToken that was created with AttachToken() or which had SetDispensable() called
  // on it.  Each group's constraints are aggregated together and succeed or fail to logically
  // allocate as a group, considered in order by when each group's constraints are ready, not in
  // order by dispensable_ordinal values.
  uint64_t next_dispensable_ordinal_ = 1;

  // Information about the current client - only valid while aggregating state for a particular
  // client.
  const NodeProperties* current_node_properties_ = nullptr;

  inspect::Node inspect_node_;
  inspect::StringProperty name_property_;
  inspect::UintProperty vmo_count_property_;
  inspect::ValueList vmo_properties_;

  // We keep LogicalBufferCollection alive as long as there are child VMOs
  // outstanding (no revoking of child VMOs for now).
  //
  // This tracking is for the benefit of MemoryAllocator sub-classes that need
  // a Delete() call, such as to clean up a slab allocation and/or to inform
  // an external allocator of delete.
  class TrackedParentVmo {
   public:
    using DoDelete = fit::callback<void(TrackedParentVmo* parent)>;
    // The do_delete callback will be invoked upon the sooner of (A) the client
    // code causing ~ParentVmo, or (B) ZX_VMO_ZERO_CHILDREN occurring async
    // after StartWait() is called.
    TrackedParentVmo(fbl::RefPtr<LogicalBufferCollection> buffer_collection, zx::vmo vmo,
                     DoDelete do_delete);
    ~TrackedParentVmo();

    // This should only be called after client code has created a child VMO, and
    // will begin the wait for ZX_VMO_ZERO_CHILDREN.
    zx_status_t StartWait(async_dispatcher_t* dispatcher);

    // Cancel the wait. This should only be used by LogicalBufferCollection
    zx_status_t CancelWait();

    zx::vmo TakeVmo();
    [[nodiscard]] const zx::vmo& vmo() const;

    void set_child_koid(zx_koid_t koid) { child_koid_ = koid; }

    TrackedParentVmo(const TrackedParentVmo&) = delete;
    TrackedParentVmo(TrackedParentVmo&&) = delete;
    TrackedParentVmo& operator=(const TrackedParentVmo&) = delete;
    TrackedParentVmo& operator=(TrackedParentVmo&&) = delete;

   private:
    void OnZeroChildren(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);
    fbl::RefPtr<LogicalBufferCollection> buffer_collection_;
    zx::vmo vmo_;
    zx_koid_t child_koid_{};
    DoDelete do_delete_;
    async::WaitMethod<TrackedParentVmo, &TrackedParentVmo::OnZeroChildren> zero_children_wait_;
    // Only for asserts:
    bool waiting_ = {};
  };

  // From buffers_remaining to server_end.
  std::multimap<uint32_t, zx::eventpair> lifetime_tracking_;

  // It's nice for members containing timers to be last for destruction order purposes, but the
  // destructor also explicitly cancels timers to avoid any brittle-ness from members potentially
  // added after these.
  using ParentVmoMap = std::map<zx_handle_t, std::unique_ptr<TrackedParentVmo>>;
  ParentVmoMap parent_vmos_;
  async::TaskMethod<LogicalBufferCollection, &LogicalBufferCollection::CreationTimedOut>
      creation_timer_{this};
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGICAL_BUFFER_COLLECTION_H_
