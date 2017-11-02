// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef JOB_SCHEDULER_H_
#define JOB_SCHEDULER_H_

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
        virtual void AtomCompleted(MsdArmAtom* atom) = 0;
    };
    JobScheduler(Owner* owner, uint32_t job_slots);

    void EnqueueAtom(std::shared_ptr<MsdArmAtom> atom);
    void TryToSchedule();

    void CancelAtomsForConnection(std::shared_ptr<MsdArmConnection> connection,
                                  std::function<void()> finished);

    void JobCompleted(uint64_t slot);

    uint32_t job_slots() const { return job_slots_; }

    size_t GetAtomListSize();

private:
    MsdArmAtom* executing_atom() const { return executing_atom_.get(); }

    Owner* owner_;

    uint32_t job_slots_;

    std::shared_ptr<MsdArmAtom> executing_atom_;
    std::list<std::shared_ptr<MsdArmAtom>> atoms_;
    std::vector<std::function<void()>> finished_callbacks_;

    friend class TestJobScheduler;

    DISALLOW_COPY_AND_ASSIGN(JobScheduler);
};

#endif // JOB_SCHEDULER_H_
