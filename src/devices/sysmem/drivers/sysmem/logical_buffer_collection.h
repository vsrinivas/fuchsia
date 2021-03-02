// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGICAL_BUFFER_COLLECTION_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGICAL_BUFFER_COLLECTION_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl-async-2/fidl_struct.h>
#include <lib/fidl/llcpp/heap_allocator.h>
#include <lib/zx/channel.h>

#include <list>
#include <map>
#include <memory>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "binding_handle.h"
#include "device.h"
#include "logging.h"

namespace sysmem_driver {

class BufferCollectionToken;
class BufferCollection;
class MemoryAllocator;

// This class can be used to hold an inspect snapshot of one set of constraints taken from a client
// at a particular point in time.
struct ConstraintInfoSnapshot {
  inspect::Node node;
  inspect::ValueList node_constraints;
};

class LogicalBufferCollection : public fbl::RefCounted<LogicalBufferCollection> {
 public:
  struct ClientInfo {
    std::string name;
    zx_koid_t id{};
  };

  // In sysmem_tests, the max needed was observed to be 12400 bytes, so if we wanted to avoid heap
  // for allocating FIDL table fields, 32KiB would likely be enough most of the time.  However, the
  // difference isn't even reliably measurable sign out of the ~410us +/- ~10us  it takes to
  // allocate a collection with only 4KiB buffer space total on astro.
  //
  // The time cost of zeroing VMO buffer space is far higher than anything controlled by this
  // number, so it'd be more fruitful to focus there before here.
  static constexpr size_t kBufferThenHeapAllocatorSize = 1;
  using FidlAllocator = fidl::BufferThenHeapAllocator<kBufferThenHeapAllocatorSize>;
  using CollectionMap = std::map<BufferCollection*, BindingHandle<BufferCollection>>;

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
                                   const ClientInfo* client_info);

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
  void CreateBufferCollectionToken(fbl::RefPtr<LogicalBufferCollection> self,
                                   uint32_t rights_attenuation_mask,
                                   zx::channel buffer_collection_token_request,
                                   const ClientInfo* client_info);

  void OnSetConstraints();

  void SetName(uint32_t priority, std::string name);
  void SetDebugTimeoutLogDeadline(int64_t deadline);

  void LogClientError(Location location, const ClientInfo* client_info, const char* format, ...)
      __PRINTFLIKE(4, 5);
  void VLogClientError(Location location, const ClientInfo* client_info, const char* format,
                       va_list args);

  struct AllocationResult {
    const llcpp::fuchsia::sysmem2::wire::BufferCollectionInfo* buffer_collection_info = nullptr;
    const zx_status_t status = ZX_OK;
  };
  AllocationResult allocation_result();

  Device* parent_device() const { return parent_device_; }

  const CollectionMap& collection_views() const { return collection_views_; }

  // The returned allocator& lasts as long as the LogicalBufferCollection, which is long enough
  // for child BufferCollection(s) to use the allocator.
  FidlAllocator& fidl_allocator() { return allocator_; }

  std::optional<std::string> name() const {
    return name_ ? std::make_optional(name_->name) : std::optional<std::string>();
  }

  inspect::Node& node() { return node_; }

 private:
  enum class CheckSanitizeStage { kInitial, kNotAggregated, kAggregated };

  struct Constraints {
    Constraints(const Constraints&) = delete;
    Constraints(Constraints&&) = default;
    Constraints(llcpp::fuchsia::sysmem2::wire::BufferCollectionConstraints&& constraints,
                ClientInfo&& client)
        : constraints(std::move(constraints)), client(std::move(client)) {}

    llcpp::fuchsia::sysmem2::wire::BufferCollectionConstraints constraints;
    ClientInfo client;
  };

  struct CollectionName {
    uint32_t priority{};
    std::string name;
  };
  using ConstraintsList = std::list<Constraints>;

  LogicalBufferCollection(Device* parent_device);

  // Will log an error. This also cleans out a lot of
  // state that's unnecessary after a failure.
  void LogAndFail(Location location, const char* format, ...) __PRINTFLIKE(3, 4);

  void Fail();

  static void LogInfo(Location location, const char* format, ...);
  static void LogErrorStatic(Location location, const ClientInfo* client_info, const char* format,
                             ...) __PRINTFLIKE(3, 4);

  // Uses the implicit |current_client_info_| to identify which client has an error.
  void LogError(Location location, const char* format, ...) __PRINTFLIKE(3, 4);
  void VLogError(Location location, const char* format, va_list args);

  void MaybeAllocate();

  void TryAllocate();

  void InitializeConstraintSnapshots(const ConstraintsList& constraints_list);

  void SetFailedAllocationResult(zx_status_t status);

  void SendAllocationResult();

  void BindSharedCollectionInternal(BufferCollectionToken* token,
                                    zx::channel buffer_collection_request);

  // To be called only by CombineConstraints().
  bool IsMinBufferSizeSpecifiedByAnyParticipant();

  bool CombineConstraints();

  bool CheckSanitizeBufferCollectionConstraints(
      CheckSanitizeStage stage,
      llcpp::fuchsia::sysmem2::wire::BufferCollectionConstraints& constraints);

  bool CheckSanitizeBufferUsage(CheckSanitizeStage stage,
                                llcpp::fuchsia::sysmem2::wire::BufferUsage& buffer_usage);

  bool CheckSanitizeBufferMemoryConstraints(
      CheckSanitizeStage stage, const llcpp::fuchsia::sysmem2::wire::BufferUsage& buffer_usage,
      llcpp::fuchsia::sysmem2::wire::BufferMemoryConstraints& constraints);

  bool CheckSanitizeImageFormatConstraints(
      CheckSanitizeStage stage, llcpp::fuchsia::sysmem2::wire::ImageFormatConstraints& constraints);

  bool AccumulateConstraintBufferCollection(
      llcpp::fuchsia::sysmem2::wire::BufferCollectionConstraints* acc,
      llcpp::fuchsia::sysmem2::wire::BufferCollectionConstraints* c);

  bool AccumulateConstraintsBufferUsage(llcpp::fuchsia::sysmem2::wire::BufferUsage* acc,
                                        llcpp::fuchsia::sysmem2::wire::BufferUsage* c);

  bool AccumulateConstraintHeapPermitted(
      fidl::VectorView<llcpp::fuchsia::sysmem2::wire::HeapType>* acc,
      fidl::VectorView<llcpp::fuchsia::sysmem2::wire::HeapType>* c);

  bool AccumulateConstraintBufferMemory(llcpp::fuchsia::sysmem2::wire::BufferMemoryConstraints* acc,
                                        llcpp::fuchsia::sysmem2::wire::BufferMemoryConstraints* c);

  bool AccumulateConstraintImageFormats(
      fidl::VectorView<llcpp::fuchsia::sysmem2::wire::ImageFormatConstraints>* acc,
      fidl::VectorView<llcpp::fuchsia::sysmem2::wire::ImageFormatConstraints>* c);

  bool AccumulateConstraintImageFormat(llcpp::fuchsia::sysmem2::wire::ImageFormatConstraints* acc,
                                       llcpp::fuchsia::sysmem2::wire::ImageFormatConstraints* c);

  bool AccumulateConstraintColorSpaces(
      fidl::VectorView<llcpp::fuchsia::sysmem2::wire::ColorSpace>* acc,
      fidl::VectorView<llcpp::fuchsia::sysmem2::wire::ColorSpace>* c);

  size_t InitialCapacityOrZero(CheckSanitizeStage stage, size_t initial_capacity);

  bool IsColorSpaceEqual(const llcpp::fuchsia::sysmem2::wire::ColorSpace& a,
                         const llcpp::fuchsia::sysmem2::wire::ColorSpace& b);

  fit::result<llcpp::fuchsia::sysmem2::wire::BufferCollectionInfo, zx_status_t> Allocate(
      fidl::Allocator& result_allocator);

  fit::result<zx::vmo> AllocateVmo(
      MemoryAllocator* allocator,
      const llcpp::fuchsia::sysmem2::wire::SingleBufferSettings& settings, uint32_t index);

  int32_t CompareImageFormatConstraintsTieBreaker(
      const llcpp::fuchsia::sysmem2::wire::ImageFormatConstraints& a,
      const llcpp::fuchsia::sysmem2::wire::ImageFormatConstraints& b);

  int32_t CompareImageFormatConstraintsByIndex(uint32_t index_a, uint32_t index_b);
  void CreationTimedOut(async_dispatcher_t* dispatcher, async::TaskBase* task, zx_status_t status);

  Device* parent_device_ = nullptr;

  FidlAllocator allocator_;

  using TokenMap = std::map<BufferCollectionToken*, BindingHandle<BufferCollectionToken>>;
  TokenMap token_views_;

  CollectionMap collection_views_;

  ConstraintsList constraints_list_;

  std::vector<ConstraintInfoSnapshot> constraints_at_allocation_;

  bool is_allocate_attempted_ = false;

  std::optional<llcpp::fuchsia::sysmem2::wire::BufferCollectionConstraints> constraints_;

  // Iff true, initial allocation has been attempted and has succeeded or
  // failed.  Both allocation_result_status_ and allocation_result_info_ are
  // not meaningful until has_allocation_result_ is true.
  bool has_allocation_result_ = false;
  zx_status_t allocation_result_status_ = ZX_OK;
  std::optional<llcpp::fuchsia::sysmem2::wire::BufferCollectionInfo> allocation_result_info_;

  MemoryAllocator* memory_allocator_ = nullptr;
  std::optional<CollectionName> name_;

  // Information about the current client - only valid while aggregating state for a particular
  // client.
  ClientInfo* current_client_info_ = nullptr;

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
  using ParentVmoMap = std::map<zx_handle_t, std::unique_ptr<TrackedParentVmo>>;
  ParentVmoMap parent_vmos_;
  async::TaskMethod<LogicalBufferCollection, &LogicalBufferCollection::CreationTimedOut>
      creation_timer_{this};

  inspect::Node node_;
  inspect::StringProperty name_property_;
  inspect::UintProperty vmo_count_property_;
  inspect::ValueList vmo_properties_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_LOGICAL_BUFFER_COLLECTION_H_
