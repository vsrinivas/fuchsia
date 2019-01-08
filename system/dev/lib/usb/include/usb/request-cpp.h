// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "usb-request.h"

#include <optional>

#include <ddk/phys-iter.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/thread_annotations.h>

// TODO(surajmalhotra): Add support for private node storage.
// TODO(surajmalhotra): Add support for auto-calling complete in destructor for
// usb::UnownedRequest if it wasn't already called.

namespace usb {

// Usage notes:
//
// usb::Request is a c++ wrapper around the usb_request_t object. It provides
// capabilites to interact with a request buffer which is used to traverse the
// usb stack. On deletion, it will automatically call |usb_request_release|.
// Most of it's functionality is defined in usb::RequestBase.
//
// usb::UnownedRequest provides an unowned variant of usb::Request. It does not
// take any action on deletion (TODO: Auto-complete if it hasn't already been
// completed on deletion). In addition, it adds a wrapper around
// |usb_request_complete| which isn't present in usb::Request.
//
// usb::RequestPool provides pooling functionality for usb::Request reuse.
//
// usb::RequestQueue provides a queue interface for tracking usb::Request and
// usb::UnownedRequest objects.
//
///////////////////////////////////////////////////////////////////////////////
// Example: Basic allocation with a pool:
//
// usb::RequestPool pool;
//
// for (int i = 0; i < kNumRequest; i++) {
//     std::optional<usb::Request> request;
//     status = usb::Request::Alloc(&request, data_size, ep_address, req_size,
//                                  parent_req_size);
//
//     if (status != ZX_OK) return status;
//     pool.add(std::move(*request));
// }
//
///////////////////////////////////////////////////////////////////////////////
// Example: Enqueue incoming requests into a usb::RequestQueue:
//
// class Driver {
// public:
//     <...>
// private:
//     usb::UnownedRequestQueue requests_;
//     const size_t parent_req_size_;
// };
//
// void Driver::UsbRequestQueue(usb_request_t* req,
//                              usb_request_queue_callback cb, void* ctx) {
//     requests_.push(usb::UnownedRequest(request, parent_req_size_));
// }
//

template <typename T>
class RequestNode;

template <typename D>
class RequestBase {
public:
    RequestBase(usb_request_t* request, size_t parent_req_size)
        : request_(request),
          node_offset_(fbl::round_up(parent_req_size, kAlignment)) {
        ZX_DEBUG_ASSERT(request != nullptr);
    }

    RequestBase(RequestBase&& other)
        : request_(other.request_), node_offset_(other.node_offset_) {
        other.request_ = nullptr;
    }

    RequestBase& operator=(RequestBase&& other) {
        request_ = other.request_;
        node_offset_ = other.node_offset_;
        other.request_ = nullptr;
        return *this;
    }

    usb_request_t* release() {
        auto* tmp = request_;
        request_ = nullptr;
        return tmp;
    }

    const usb_request_t* request() {
        return request_;
    }

    // Initializes the statically allocated usb request with the given VMO.
    // This will free any resources allocated by the usb request but not the usb request itself.
    zx_status_t Init(const zx::vmo& vmo, uint64_t vmo_offset, uint64_t length, uint8_t ep_address) {
        return usb_request_init(request_, vmo.get(), vmo_offset, length, ep_address);
    }

    // Copies the scatter gather list to the request.
    // Future transfers using this request will determine where in the VMO to store read/write data.
    // using the scatter gather list.
    // This will free any existing scatter gather list stored in the request.
    zx_status_t SetScatterGatherList(const phys_iter_sg_entry_t* sg_list, size_t sg_count) {
        return usb_request_set_sg_list(request_, sg_list, sg_count);
    }

    // Copies data from the Request's vm object.
    // Out of range operations are ignored.
    ssize_t CopyFrom(void* data, size_t length, size_t offset) {
        return usb_request_copy_from(request_, data, length, offset);
    }

    // Copies data into a Request's vm object.
    // Out of range operations are ignored.
    ssize_t CopyTo(const void* data, size_t length, size_t offset) {
        return usb_request_copy_to(request_, data, length, offset);
    }

    // Maps the Request's vm object. The 'data' field is set with the mapped address if this
    // function succeeds.
    zx_status_t Mmap(void** data) {
        return usb_request_mmap(request_, data);
    }

    // Performs a cache maintenance op against the request's internal buffer.
    zx_status_t CacheOp(uint32_t op, size_t offset, size_t length) {
        return usb_request_cacheop(request_, op, offset, length);
    }

    // Performs a cache flush on a range of memory in the request's buffer.
    zx_status_t CacheFlush(zx_off_t offset, size_t length) {
        return usb_request_cache_flush(request_, offset, length);
    }

    // Performs a cache flush and invalidate on a range of memory in the request's buffer.
    zx_status_t CacheFlushInvalidate(zx_off_t offset, size_t length) {
        return usb_request_cache_flush_invalidate(request_, offset, length);
    }

    // Looks up the physical pages backing this request's vm object.
    zx_status_t PhysMap(const zx::bti& bti) {
        return usb_request_physmap(request_, bti.get());
    }

    // Initializes a ddk::PhysIter for a usb request.
    // |max_length| is the maximum length of a range returned the iterator.
    // |max_length| must be either a positive multiple of PAGE_SIZE, or zero for no limit.
    ddk::PhysIter phys_iter(size_t max_length) {
        phys_iter_buffer_t buf = {
            .phys = request_->phys_list,
            .phys_count = request_->phys_count,
            .length = request_->header.length,
            .vmo_offset = request_->offset,
            .sg_list = request_->sg_list,
            .sg_count = request_->sg_count};
        return ddk::PhysIter(buf, max_length);
    }

    static constexpr size_t RequestSize(size_t parent_req_size) {
        return fbl::round_up(parent_req_size, kAlignment) +
               fbl::round_up(sizeof(RequestNode<D>), kAlignment);
    }

    size_t size() const {
        return node_offset_ + fbl::round_up(sizeof(RequestNode<D>), kAlignment);
    }

    // Returns private node stored inline
    RequestNode<D>* node() {
        auto* node = reinterpret_cast<RequestNode<D>*>(reinterpret_cast<uintptr_t>(request_) +
                                                       node_offset_);
        new (node) RequestNode<D>(node_offset_);
        return node;
    }

    template <typename T>
    T* private_storage() {
        return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(request_) + size());
    }

protected:
    usb_request_t* request_;
    zx_off_t node_offset_;

private:
    static constexpr size_t kAlignment = 8;
};

class Request : public RequestBase<Request> {
public:
    // Creates a new usb request with payload space of data_size.
    static zx_status_t Alloc(std::optional<Request>* out, uint64_t data_size,
                             uint8_t ep_address, size_t req_size,
                             size_t parent_req_size = sizeof(usb_request_t)) {
        usb_request_t* request;
        zx_status_t status = usb_request_alloc(&request, data_size, ep_address, req_size);
        if (status == ZX_OK) {
            *out = Request(request, parent_req_size);
        } else {
            *out = std::nullopt;
        }
        return status;
    }

    // Creates a new usb request with the given VMO.
    static zx_status_t AllocVmo(std::optional<Request>* out, const zx::vmo& vmo,
                                uint64_t vmo_offset, uint64_t length, uint8_t ep_address,
                                size_t req_size, size_t parent_req_size = sizeof(usb_request_t)) {
        usb_request_t* request;
        zx_status_t status = usb_request_alloc_vmo(&request, vmo.get(), vmo_offset, length,
                                                   ep_address, req_size);
        if (status == ZX_OK) {
            *out = Request(request, parent_req_size);
        } else {
            *out = std::nullopt;
        }
        return status;
    }

    Request(usb_request_t* request, size_t parent_req_size)
        : RequestBase(request, parent_req_size) {}

    Request(Request&& other)
        : RequestBase(other.request_, other.node_offset_) {
        other.request_ = nullptr;
    }

    Request& operator=(Request&& other) {
        RequestBase<Request>::operator=(std::move(other));
        return *this;
    }

    ~Request() {
        Release();
    }

    void Release() {
        if (request_) {
            usb_request_release(request_);
            request_ = nullptr;
        }
    }
};

// Similar to usb::Request, but it doesn't call usb_request_release on delete.
// This should be used to wrap usb_request_t* objects allocated in other
// drivers.
class UnownedRequest : public RequestBase<UnownedRequest> {
public:
    UnownedRequest(usb_request_t* request, size_t parent_req_size)
        : RequestBase(request, parent_req_size) {}

    UnownedRequest(UnownedRequest&& other)
        : RequestBase(other.request_, other.node_offset_) {
        other.request_ = nullptr;
    }

    UnownedRequest& operator=(UnownedRequest&& other) {
        RequestBase<UnownedRequest>::operator=(std::move(other));
        return *this;
    }

    // Must be called by the processor when the request has
    // completed or failed and the request and any virtual or physical memory obtained
    // from it may not be touched again by the processor.
    //
    // The complete_cb() will be called as the last action of this method.
    void Complete(zx_status_t status, zx_off_t actual, const usb_request_complete_t* complete_cb) {
        usb_request_complete(request_, status, actual, complete_cb);
        request_ = nullptr;
    }
};

// Node storage for usb::Request and usb::UnownedRequest. Does not maintain
// ownership of underlying usb_request_t*. Must be transformed back into
// appopriate wrapper type to maintain correct ownership.
// It is strongly recommended to use usb::RequestPool and usb::RequestQueue to
// avoid ownership pitfalls.
template <typename T>
class RequestNode : public fbl::DoublyLinkedListable<RequestNode<T>*> {
public:
    RequestNode(zx_off_t node_offset)
        : node_offset_(node_offset) {}

    ~RequestNode() = default;

    T request() const {
        return T(
            reinterpret_cast<usb_request_t*>(reinterpret_cast<uintptr_t>(this) - node_offset_),
            node_offset_);
    }

private:
    const zx_off_t node_offset_;
};

// A driver may use usb::RequestPool for recycling their own usb requests.
class RequestPool {
public:
    RequestPool() {}

    ~RequestPool() {
        Release();
    }

    // Adds the request to the pool.
    void Add(Request req) {
        fbl::AutoLock al(&lock_);
        RequestNode<Request>* node = req.node();
        free_reqs_.push_front(node);
        req.release();
    }

    // Returns a request from the pool that has a buffer of the given length,
    // or null if no such request exists.
    // The request is not re-initialized in any way and should be set accordingly by the user.
    std::optional<Request> Get(size_t length) {
        fbl::AutoLock al(&lock_);
        auto node = free_reqs_.erase_if([length](const RequestNode<Request>& node) {
            auto request = node.request();
            const size_t size = request.size();
            request.release(); // Don't free request.
            return size == length;
        });
        if (node) {
            return node->request();
        }
        return std::nullopt;
    }

    // Releases all usb requests stored in the pool.
    void Release() {
        fbl::AutoLock al(&lock_);
        while (!free_reqs_.is_empty()) {
            __UNUSED auto req = free_reqs_.pop_front()->request();
        }
    }

private:
    fbl::Mutex lock_;
    fbl::DoublyLinkedList<RequestNode<Request>*> free_reqs_ TA_GUARDED(lock_);
};

// Conveniance queue wrapper around fbl::DoublyLinkedList<T>.
template <typename T>
class BaseQueue {
public:
    BaseQueue() {}

    ~BaseQueue() {
        Release();
    }

    void push(T req) {
        fbl::AutoLock al(&lock_);
        RequestNode<T>* node = req.node();
        queue_.push_front(node);
        req.release();
    }

    std::optional<T> pop() {
        fbl::AutoLock al(&lock_);
        RequestNode<T>* node = queue_.pop_back();
        if (node) {
            return std::move(node->request());
        }
        return std::nullopt;
    }

    // Releases all usb requests stored in the queue.
    void Release() {
        fbl::AutoLock al(&lock_);
        while (!queue_.is_empty()) {
            __UNUSED auto req = queue_.pop_front()->request();
        }
    }

private:
    fbl::Mutex lock_;
    fbl::DoublyLinkedList<RequestNode<T>*> queue_ TA_GUARDED(lock_);
};

using UnownedRequestQueue = BaseQueue<UnownedRequest>;
using RequestQueue = BaseQueue<Request>;

} // namespace usb
