// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef JOB_SCHEDULER_H_
#define JOB_SCHEDULER_H_

#include <chrono>
#include <functional>
#include <list>
#include <vector>

#include "magma_util/macros.h"
#include "msd_arm_atom.h"

class JobScheduler {
public:
    class Owner {
    public:
        virtual void RunAtom(MsdArmAtom* atom) = 0;
        virtual void AtomCompleted(MsdArmAtom* atom, ArmMaliResultCode result_code) = 0;
        virtual void HardStopAtom(MsdArmAtom* atom) {}
        virtual void ReleaseMappingsForAtom(MsdArmAtom* atom) {}
        virtual magma::PlatformPort* GetPlatformPort() { return nullptr; }
    };
    using Clock = std::chrono::steady_clock;

    JobScheduler(Owner* owner, uint32_t job_slots);

    void EnqueueAtom(std::shared_ptr<MsdArmAtom> atom);
    void TryToSchedule();
    void PlatformPortSignaled(uint64_t key);

    void CancelAtomsForConnection(std::shared_ptr<MsdArmConnection> connection);

    void JobCompleted(uint64_t slot, ArmMaliResultCode result_code);

    uint32_t job_slots() const { return job_slots_; }

    size_t GetAtomListSize();

    // Gets the duration until the currently executing atom should time out,
    // or max if there's no timeout pending.
    Clock::duration GetCurrentTimeoutDuration();

    void KillTimedOutAtoms();

    void ReleaseMappingsForConnection(std::shared_ptr<MsdArmConnection> connection);

private:
    MsdArmAtom* executing_atom() const { return executing_atom_.get(); }
    void ProcessSoftAtom(std::shared_ptr<MsdArmSoftAtom> atom);

    void set_timeout_duration(uint64_t timeout_duration_ms)
    {
        timeout_duration_ms_ = timeout_duration_ms;
    }

    Owner* owner_;

    uint32_t job_slots_;

    uint64_t timeout_duration_ms_ = 1000;

    std::vector<std::shared_ptr<MsdArmSoftAtom>> waiting_atoms_;
    std::shared_ptr<MsdArmAtom> executing_atom_;
    std::list<std::shared_ptr<MsdArmAtom>> atoms_;

    friend class TestJobScheduler;

    DISALLOW_COPY_AND_ASSIGN(JobScheduler);
};

#endif // JOB_SCHEDULER_H_
