// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/syscalls/port.h>
#include <magenta/types.h>
#include <mx/channel.h>
#include <mx/handle.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>
#include <unistd.h>

namespace audio {

class DispatcherChannel;
using DispatcherChannelAllocTraits =
    fbl::StaticSlabAllocatorTraits<fbl::RefPtr<DispatcherChannel>>;
using DispatcherChannelAllocator = fbl::SlabAllocator<DispatcherChannelAllocTraits>;

// class DispatcherChannel
//
// DispatcherChannels are objects used in the dispatcher-pool framework to
// manage a magenta channel endpoint, and its relationship to the dispatcher
// thread pool (@see DispatcherThread) and the channel endpoint's Owner (@see
// DispatcherChannel::Owner).
//
// DispatcherChannels begin their lives as empty objects.  They have neither an
// Owner, nor a channel endpoint until they become Activated.  During
// Activation, DispatcherChannels are...
//
// ## Assigned an Owner.
// ## Take ownership of a channel endpoint.
// ## Join the set of active channels being managed by the global dispatcher
//    thread pool (binding their channel endpoint to the thread pool's port in
//    the process).
//
// There are two ways to Activate a DispatcherChannel.  One may either give the
// DispatcherChannel a channel endpoint to manage, or one may have the
// Dispatcher channel create a new channel, claim one of the endpoints and give
// the other endpoint back to the caller.  In both cases, a reference to the
// channel's new owner must be supplied.  One may not Activate an already active
// channel; attempting to do so will return an error and the channel will remain
// active.
//
// Once activated, DispatcherChannels will notify their Owners via a callback
// dispatched from the global thread pool when...
//
// ## The channel has pending messages available for read
//    (DispatcherChannel::Owner::ProcessChannel)
// ## The channel has become deactivated.
//    (DispatcherChannel::Owner::NotifyChannelDeactivated)
//
// No locks are held during any of these callbacks.  A channel *may* become
// de-activated during a call to an Owner's process channel, at which point in
// time calls to channel.Read and channel.Write will begin to fail.
//
// An Owner's ProcessChannel method will never be called concurrently for the
// same specific channel, but may be called concurrently for two different
// channels owned by the same Owner.  IOW - If an Owner owns channels A and B,
// there will never be multiple call to owner.ProcessChannel(ChA, ...) in flight
// at once, but there can be a call to owner.ProcessChannel(ChA, ...) in flight
// at the same time as a call to owner.ProcessChannel(ChB, ...).
//
// TODO(johngro): Make this true.   Currently, we cannot guarantee the above
// statement with the ports V1 implementation (we will be able to once we
// transition to ports V2).
//
// There are three ways to deactivate a channel once it has become activated.
//
// ## Explicitly call the Deactivate method of the channel.
// ## Implicitly deactivate the channel by signalling any error from a call to
//    Owner::ProcessChannel
// ## Explicitly shut down all of an Owner's channels with the
//    ShutdownDispatcherChannels method of DispatcherChannel::Owner.
//
// When a channel becomes deactivated, it will (by default) notify its Owner
// with a call to DispatcherChannel::Owner::NotifyChannelDeactivated.  A
// callback will only be generated if the channel was active at the time of
// deactivation.  This behavior can be overridden by setting the 'do_notify'
// parameter of the explicit deactivation method to false, in which case no
// callback will be generated regardless of the activation state of the
// DispatcherChannel.
//
// Both of the explicit deactivation methods are safe to call from within the
// context of an owner's ProcessChannel callback, however any
// NotifyChannelDeactivated callbacks will take place within the context of the
// call to channel.Deactivate or owner.ShutdownDispatcherChannels.  Owner
// implementers who are holding synchronization primitives at the time of
// deactivation should request that no-callbacks be generated and handle cleanup
// of their internal state after deactivation has finished.
//
// TODO(johngro) : In a follow-up change, go back and add a 'do_notify'
// parameter to ShutdownDispatcherChannels.
//
// Deactivation of a channel automatically closes the underlying channel
// endpoint handle, but does not automatically synchronize with any callbacks in
// flight.  Because of this channels should not be re-activated; user should
// release their old DispatcherChannel reference and allocate a new one instead
// of attempting to re-use their DispatcherChannel objects.
//
// TODO(johngro) : In a follow-up change, just change this so that
// DispatcherChannels are one-time-activation only so that users cannot make
// this mistake.
//
class DispatcherChannel : public fbl::RefCounted<DispatcherChannel>,
                          public fbl::SlabAllocated<DispatcherChannelAllocTraits> {
public:
    // class DispatcherChannel::Owner
    //
    // DispatcherChannel::Owner defines the interface implemented by users of
    // DispatcherChannels in order to receive notifications of channels having
    // messages ready to be processed, and of channels being closed.  @see
    // DispatcherChannel for more details.  In addition to the information
    // highlighted there, note that...
    //
    // ## DispatcherChannel::Owners are ref-counted objects, and that the
    //    ref-count is implemented by by the DispatcherChannel::Owner base
    //    class.
    // ## DispatcherChannel::Owners *must* implement ProcessChannel.
    // ## DispatcherChannel::Owners *may* implement NotifyChannelDeactivated,
    //    but are not required to.
    class Owner : public fbl::RefCounted<Owner> {
    protected:
        friend class fbl::RefPtr<Owner>;
        friend class DispatcherChannel;

        virtual ~Owner() {
            // Assert that the Owner implementation properly deactivated itself
            // before destructing.
            MX_DEBUG_ASSERT(deactivated_);
            MX_DEBUG_ASSERT(channels_.is_empty());
        }

        void ShutdownDispatcherChannels() __TA_EXCLUDES(channels_lock_);

        // ProcessChannel
        //
        // Called by the thread pool infrastructure to notify an owner that
        // there is a message pending on the channel.  Returning any error at
        // this point in time will cause the channel to be deactivated and
        // be released.
        virtual mx_status_t ProcessChannel(DispatcherChannel* channel)
            __TA_EXCLUDES(channels_lock_) = 0;

        // NotifyChannelDeactivated.
        //
        // Called by the thread pool infrastructure to notify an owner that a
        // channel is has become deactivated.  No new ProcessChannel callbacks
        // will arrive from 'channel', but it is possible that there are still
        // some callbacks currently in flight.  DispatcherChannel::Owner
        // implementers should take whatever synchronization steps are
        // appropriate.
        virtual void NotifyChannelDeactivated(const DispatcherChannel& channel)
            __TA_EXCLUDES(channels_lock_) { }

    private:
        mx_status_t AddChannel(fbl::RefPtr<DispatcherChannel>&& channel)
            __TA_EXCLUDES(channels_lock_);
        void RemoveChannel(DispatcherChannel* channel)
            __TA_EXCLUDES(channels_lock_);

        // Allow the deactivated flag to be checked without obtaining the
        // channels_lock_.
        //
        // The deactivated_ flag may never be cleared once set.  It needs to be
        // checked and held static during operations like adding and removing
        // channels, as well as deactivating the owner.  While processing and
        // dispatching messages, however (DispatcherChannel::Process), a
        // channel's owner might become deactivated, and it should be OK to
        // perform a simple spot check (without obtaining the lock) and
        // fast-abort if the owner was disabled.
        bool deactivated() const __TA_NO_THREAD_SAFETY_ANALYSIS {
            return static_cast<volatile bool>(deactivated_);
        }

        fbl::Mutex channels_lock_;
        bool deactivated_ __TA_GUARDED(channels_lock_) = false;
        fbl::DoublyLinkedList<fbl::RefPtr<DispatcherChannel>> channels_
            __TA_GUARDED(channels_lock_);
    };

    static fbl::RefPtr<DispatcherChannel> GetActiveChannel(uint64_t id)
        __TA_EXCLUDES(active_channels_lock_) {
        fbl::AutoLock channels_lock(&active_channels_lock_);
        return GetActiveChannelLocked(id);
    }

    uint64_t  bind_id()            const { return bind_id_; }
    uint64_t  GetKey()             const { return bind_id(); }
    uintptr_t owner_ctx()          const { return owner_ctx_; }
    bool      InOwnersList()       const { return dll_node_state_.InContainer(); }
    bool      InActiveChannelSet() const { return wavl_node_state_.InContainer(); }

    mx_status_t WaitOnPort(const mx::port& port) __TA_EXCLUDES(obj_lock_, active_channels_lock_) {
        fbl::AutoLock obj_lock(&obj_lock_);
        return WaitOnPortLocked(port);
    }

    mx_status_t Activate(fbl::RefPtr<Owner>&& owner, mx::channel* client_channel_out)
        __TA_EXCLUDES(obj_lock_, active_channels_lock_);

    mx_status_t Activate(fbl::RefPtr<Owner>&& owner, mx::channel&& client_channel)
        __TA_EXCLUDES(obj_lock_, active_channels_lock_) {
        fbl::AutoLock obj_lock(&obj_lock_);
        return ActivateLocked(fbl::move(owner), fbl::move(client_channel));
    }

    void Deactivate(bool do_notify) __TA_EXCLUDES(obj_lock_, active_channels_lock_);
    mx_status_t Process(const mx_port_packet_t& port_packet)
        __TA_EXCLUDES(obj_lock_, active_channels_lock_);
    mx_status_t Read(void* buf,
                     uint32_t buf_len,
                     uint32_t* bytes_read_out,
                     mx::handle* rxed_handle = nullptr) const
        __TA_EXCLUDES(obj_lock_, active_channels_lock_);
    mx_status_t Write(const void* buf,
                      uint32_t buf_len,
                      mx::handle&& tx_handle = mx::handle()) const
        __TA_EXCLUDES(obj_lock_, active_channels_lock_);

private:
    friend DispatcherChannelAllocator;
    friend class  fbl::RefPtr<DispatcherChannel>;
    friend struct fbl::DefaultDoublyLinkedListTraits<fbl::RefPtr<DispatcherChannel>>;
    friend struct fbl::DefaultWAVLTreeTraits<fbl::RefPtr<DispatcherChannel>>;

    static fbl::RefPtr<DispatcherChannel> GetActiveChannelLocked(uint64_t id)
        __TA_REQUIRES(active_channels_lock_) {
        auto iter = active_channels_.find(id);
        return iter.IsValid() ? iter.CopyPointer() : nullptr;
    }

    DispatcherChannel(uintptr_t owner_ctx = 0);
    ~DispatcherChannel();

    mx_status_t WaitOnPortLocked(const mx::port& port)
        __TA_REQUIRES(obj_lock_);

    mx_status_t ActivateLocked(fbl::RefPtr<Owner>&& owner, mx::channel&& channel)
        __TA_REQUIRES(obj_lock_);

    fbl::RefPtr<Owner>  owner_    __TA_GUARDED(obj_lock_);
    mx::channel          channel_  __TA_GUARDED(obj_lock_);
    mutable fbl::Mutex  obj_lock_ __TA_ACQUIRED_BEFORE(active_channels_lock_,
                                                        owner_->channels_lock_);
    const bool           client_thread_active_;
    const uint64_t       bind_id_;
    const uintptr_t      owner_ctx_;

    // Node state for existing on the Owner's channels_ list.
    fbl::DoublyLinkedListNodeState<fbl::RefPtr<DispatcherChannel>> dll_node_state_;

    // Node state for in the active_channels_ set.
    fbl::WAVLTreeNodeState<fbl::RefPtr<DispatcherChannel>> wavl_node_state_;

    static fbl::Mutex active_channels_lock_;
    static fbl::WAVLTree<uint64_t, fbl::RefPtr<DispatcherChannel>>
        active_channels_ __TA_GUARDED(active_channels_lock_);
};

}  // namespace audio

FWD_DECL_STATIC_SLAB_ALLOCATOR(::audio::DispatcherChannelAllocTraits);

