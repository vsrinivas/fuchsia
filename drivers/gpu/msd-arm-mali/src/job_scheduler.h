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

    void EnqueueAtom(std::unique_ptr<MsdArmAtom> atom);
    void TryToSchedule();

    void CancelAtomsForConnection(std::shared_ptr<MsdArmConnection> connection,
                                  std::function<void()> finished);

    void JobCompleted(uint64_t slot);

    uint32_t job_slots() const { return job_slots_; }

    size_t GetAtomListSize();

private:
    Owner* owner_;

    uint32_t job_slots_;

    bool running_ = false;
    std::list<std::unique_ptr<MsdArmAtom>> atoms_;
    std::vector<std::function<void()>> finished_callbacks_;

    DISALLOW_COPY_AND_ASSIGN(JobScheduler);
};

#endif // JOB_SCHEDULER_H_
