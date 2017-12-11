// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

typedef struct zx_vcpu_state zx_vcpu_state_t;
typedef struct zx_vcpu_create_args zx_vcpu_create_args_t;

class Guest;

class Vcpu {
public:
    enum class State {
        // No kernel objects have been created.
        UNINITIALIZED = 0,

        // A handle to the VCPU has been obtained but the thread has not yet
        // begun execution.
        WAITING_TO_START = 1,

        // The VCPU is in the process of starting execution.
        STARTING = 2,

        // The VCPU is running in the guest context. VCPU packets are being
        // processed.
        STARTED = 3,

        // The VCPU has been terminated gracefully.
        TERMINATED = 4,

        // A failure was encountered while creating the VCPU.
        ERROR_FAILED_TO_CREATE = 5,

        // A failure was encontered while starting the VCPU.
        ERROR_FAILED_TO_START = 6,

        // A failure was encontered while resuming the VCPU.
        ERROR_FAILED_TO_RESUME = 7,

        // A terminal failure was encontered while handling a guest packet.
        ERROR_ABORTED = 7,
    };

    // Create a new VCPU for a given guest.
    //
    // Upon successful completion the VCPU will be in the state
    // |WAITING_TO_START|.
    zx_status_t Create(const Guest* guest, zx_vcpu_create_args_t* args);

    // Begins VCPU execution.
    //
    // If |initial_vcpu_state| is non-null the given state will be written to
    // the VCPU before execution begins.
    zx_status_t Start(zx_vcpu_state_t* initial_vcpu_state);

    // Waits for the VCPU to transition to a terminal state.
    zx_status_t Join();

    // TODO(tjdetwiler): Delete this once machina has migrated to use |Create|.
    zx_status_t Init(const Guest& guest, zx_vcpu_create_args_t* args);

    // TODO(tjdetwiler): These should be made private as they're not thread-
    // safe.
    zx_status_t Loop();
    zx_status_t Interrupt(uint32_t vector);

    zx_status_t ReadState(uint32_t kind, void* buffer, uint32_t len) const;
    zx_status_t WriteState(uint32_t kind, const void* buffer, uint32_t len);

private:
    // Entry point for the VCPU on the dedicated VCPU thread. This thread will
    // handle taking the VCPU through the entire VCPU lifecycle and handle any
    // interaction with the VCPU syscalls.
    struct ThreadEntryArgs;
    zx_status_t ThreadEntry(const ThreadEntryArgs* args);

    // Sets the VCPU state and notifies any waiters.
    void SetStateLocked(State new_state) __TA_REQUIRES(mutex_);

    // Block until |state_| != |initial_state|.
    void WaitForStateChangeLocked(State initial_state) __TA_REQUIRES(mutex_);

    thrd_t thread_;
    zx_handle_t vcpu_ = ZX_HANDLE_INVALID;
    State state_ __TA_GUARDED(mutex_) = State::UNINITIALIZED;
    cnd_t state_cnd_;
    fbl::Mutex mutex_;
    zx_vcpu_state_t* initial_vcpu_state_ __TA_GUARDED(mutex_);
};
