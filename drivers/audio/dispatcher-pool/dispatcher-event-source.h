// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>
#include <zx/handle.h>
#include <zx/port.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <unistd.h>

namespace audio {

class DispatcherChannel;

class DispatcherEventSource : public fbl::RefCounted<DispatcherEventSource> {
public:
    // class DispatcherEventSource::Owner
    //
    // DispatcherEventSource::Owner defines the interface implemented by users of
    // DispatcherEventSources in order to receive notifications of channels having
    // messages ready to be processed, and of channels being closed.  @see
    // DispatcherEventSource for more details.  In addition to the information
    // highlighted there, note that...
    //
    // ## DispatcherEventSource::Owners are ref-counted objects, and that the
    //    ref-count is implemented by by the DispatcherEventSource::Owner base
    //    class.
    // ## DispatcherEventSource::Owners *must* implement ProcessChannel.
    // ## DispatcherEventSource::Owners *may* implement NotifyChannelDeactivated,
    //    but are not required to.
    class Owner : public fbl::RefCounted<Owner> {
    protected:
        friend class fbl::RefPtr<Owner>;
        friend class DispatcherChannel;
        friend class DispatcherEventSource;

        virtual ~Owner() {
            // Assert that the Owner implementation properly deactivated itself
            // before destructing.
            ZX_DEBUG_ASSERT(deactivated_);
            ZX_DEBUG_ASSERT(sources_.is_empty());
        }

        void ShutdownDispatcherEventSources() __TA_EXCLUDES(sources_lock_);

        // TODO(johngro) : Remove this once users have been updated
        void ShutdownDispatcherChannels() __TA_EXCLUDES(sources_lock_) {
            ShutdownDispatcherEventSources();
        }

        // ProcessChannel
        //
        // Called by the thread pool infrastructure to notify an owner that
        // there is a message pending on the channel.  Returning any error at
        // this point in time will cause the channel to be deactivated and
        // be released.
        virtual zx_status_t ProcessChannel(DispatcherChannel* channel)
            __TA_EXCLUDES(sources_lock_) = 0;

        // NotifyChannelDeactivated.
        //
        // Called by the thread pool infrastructure to notify an owner that a
        // channel is has become deactivated.  No new ProcessChannel callbacks
        // will arrive from 'channel', but it is possible that there are still
        // some callbacks currently in flight.  DispatcherEventSource::Owner
        // implementers should take whatever synchronization steps are
        // appropriate.
        virtual void NotifyChannelDeactivated(const DispatcherChannel& channel)
            __TA_EXCLUDES(sources_lock_) { }

    private:
        zx_status_t AddEventSource(fbl::RefPtr<DispatcherEventSource>&& source)
            __TA_EXCLUDES(sources_lock_);
        void RemoveEventSource(DispatcherEventSource* source)
            __TA_EXCLUDES(sources_lock_);

        // Allow the deactivated flag to be checked without obtaining the
        // sources_lock_.
        //
        // The deactivated_ flag may never be cleared once set.  It needs to be
        // checked and held static during operations like adding and removing
        // channels, as well as deactivating the owner.  While processing and
        // dispatching messages, however (DispatcherEventSource::Process), a
        // channel's owner might become deactivated, and it should be OK to
        // perform a simple spot check (without obtaining the lock) and
        // fast-abort if the owner was disabled.
        bool deactivated() const __TA_NO_THREAD_SAFETY_ANALYSIS {
            return static_cast<volatile bool>(deactivated_);
        }

        fbl::Mutex sources_lock_;
        bool deactivated_ __TA_GUARDED(sources_lock_) = false;
        fbl::DoublyLinkedList<fbl::RefPtr<DispatcherEventSource>> sources_
            __TA_GUARDED(sources_lock_);
    };

    zx_signals_t process_signal_mask()    const { return process_signal_mask_; }
    zx_signals_t shutdown_signal_mask()   const { return shutdown_signal_mask_; }
    uintptr_t    owner_ctx()              const { return owner_ctx_; }
    bool         InOwnersList()           const { return dll_node_state_.InContainer(); }

    zx_status_t WaitOnPort(const zx::port& port) __TA_EXCLUDES(obj_lock_) {
        fbl::AutoLock obj_lock(&obj_lock_);
        return WaitOnPortLocked(port);
    }

    void Deactivate(bool do_notify) __TA_EXCLUDES(obj_lock_);

    zx_status_t Process(const zx_port_packet_t& port_packet) __TA_EXCLUDES(obj_lock_);

protected:
    DispatcherEventSource(zx_signals_t process_signal_mask,
                          zx_signals_t shutdown_signal_mask,
                          uintptr_t owner_ctx);
    virtual ~DispatcherEventSource();

    bool client_thread_active() const { return client_thread_active_; }
    fbl::RefPtr<Owner> DeactivateLocked() __TA_REQUIRES(obj_lock_);
    zx_status_t WaitOnPortLocked(const zx::port& port) __TA_REQUIRES(obj_lock_);

    virtual zx_status_t ProcessInternal(const fbl::RefPtr<Owner>& owner,
                                        const zx_port_packet_t& port_packet)
        __TA_EXCLUDES(obj_lock_) = 0;

    virtual void NotifyDeactivated(const fbl::RefPtr<Owner>& owner)
        __TA_EXCLUDES(obj_lock_) = 0;

    mutable fbl::Mutex obj_lock_ __TA_ACQUIRED_BEFORE(owner_->sources_lock_);
    fbl::RefPtr<Owner> owner_    __TA_GUARDED(obj_lock_);
    zx::handle          handle_   __TA_GUARDED(obj_lock_);

private:
    friend class  fbl::RefPtr<DispatcherEventSource>;
    friend struct fbl::DefaultDoublyLinkedListTraits<fbl::RefPtr<DispatcherEventSource>>;
    friend struct fbl::DefaultWAVLTreeTraits<fbl::RefPtr<DispatcherEventSource>>;

    const bool          client_thread_active_;
    const zx_signals_t  process_signal_mask_;
    const zx_signals_t  shutdown_signal_mask_;
    const uintptr_t     owner_ctx_;

    bool wait_pending_ __TA_GUARDED(obj_lock_) = false;

    // Node state for existing on the Owner's channels_ list.
    fbl::DoublyLinkedListNodeState<fbl::RefPtr<DispatcherEventSource>> dll_node_state_;
};

}  // namespace audio
