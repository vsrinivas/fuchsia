// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "job_scheduler.h"

#include "magma_util/dlog.h"

JobScheduler::JobScheduler(Owner* owner, uint32_t job_slots) : owner_(owner), job_slots_(job_slots)
{
}

void JobScheduler::EnqueueAtom(std::shared_ptr<MsdArmAtom> atom)
{
    atoms_.push_back(std::move(atom));
}

void JobScheduler::TryToSchedule()
{
    while (!executing_atom_) {
        if (atoms_.empty())
            return;
        for (auto it = atoms_.begin(); it != atoms_.end(); ++it) {
            if ((*it)->AreDependenciesFinished()) {
                executing_atom_ = *it;
                atoms_.erase(it);
                break;
            } else {
                DLOG("Skipping atom %lx due to dependency", (*it)->gpu_address());
            }
        }

        if (executing_atom_) {
            executing_atom_->SetExecutionStarted();
            auto soft_atom = MsdArmSoftAtom::cast(executing_atom_);
            if (soft_atom) {
                ProcessSoftAtom(soft_atom);
                continue;
            }
            owner_->RunAtom(executing_atom_.get());
        }
        return;
    }
}

void JobScheduler::CancelAtomsForConnection(std::shared_ptr<MsdArmConnection> connection)
{
    auto removal_function = [connection](auto it) {
        auto locked = it->connection().lock();
        return !locked || locked == connection;
    };
    waiting_atoms_.erase(
        std::remove_if(waiting_atoms_.begin(), waiting_atoms_.end(), removal_function),
        waiting_atoms_.end());

    atoms_.remove_if(removal_function);
}

void JobScheduler::JobCompleted(uint64_t slot, ArmMaliResultCode result_code)
{
    // Ignore slot, because only one job can be running at a time.
    DASSERT(executing_atom_);
    owner_->AtomCompleted(executing_atom_.get(), result_code);
    executing_atom_.reset();
    TryToSchedule();
}

void JobScheduler::PlatformPortSignaled(uint64_t key)
{
    std::vector<std::shared_ptr<MsdArmSoftAtom>> unfinished_atoms;
    bool completed_atom = false;
    for (auto& atom : waiting_atoms_) {
        bool wait_succeeded;
        if (atom->soft_flags() == kAtomFlagSemaphoreWait) {
            wait_succeeded = atom->platform_semaphore()->WaitNoReset(0);
        } else {
            DASSERT(atom->soft_flags() == kAtomFlagSemaphoreWaitAndReset);
            wait_succeeded = atom->platform_semaphore()->Wait(0);
        }

        if (wait_succeeded) {
            completed_atom = true;
            owner_->AtomCompleted(atom.get(), kArmMaliResultSuccess);
        } else {
            if (atom->platform_semaphore()->id() == key)
                atom->platform_semaphore()->WaitAsync(owner_->GetPlatformPort());
            unfinished_atoms.push_back(atom);
        }
    }
    if (completed_atom) {
        waiting_atoms_ = unfinished_atoms;
        TryToSchedule();
    }
}

size_t JobScheduler::GetAtomListSize() { return atoms_.size(); }

JobScheduler::Clock::duration JobScheduler::GetCurrentTimeoutDuration()
{
    if (!executing_atom_ || executing_atom_->hard_stopped())
        return Clock::duration::max();
    return executing_atom_->execution_start_time() +
           std::chrono::milliseconds(timeout_duration_ms_) - Clock::now();
}

void JobScheduler::KillTimedOutAtoms()
{
    if (GetCurrentTimeoutDuration() <= Clock::duration::zero()) {
        DASSERT(executing_atom_.get());
        executing_atom_->set_hard_stopped();
        owner_->HardStopAtom(executing_atom_.get());
    }
}

void JobScheduler::ProcessSoftAtom(std::shared_ptr<MsdArmSoftAtom> atom)
{
    DASSERT(owner_->GetPlatformPort());
    DASSERT(atom == executing_atom_);
    if (atom->soft_flags() == kAtomFlagSemaphoreSet) {
        atom->platform_semaphore()->Signal();
        JobCompleted(atom->slot(), kArmMaliResultSuccess);
    } else if (atom->soft_flags() == kAtomFlagSemaphoreReset) {
        atom->platform_semaphore()->Reset();
        JobCompleted(atom->slot(), kArmMaliResultSuccess);
    } else if ((atom->soft_flags() == kAtomFlagSemaphoreWait) ||
               (atom->soft_flags() == kAtomFlagSemaphoreWaitAndReset)) {
        bool wait_succeeded;
        if (atom->soft_flags() == kAtomFlagSemaphoreWait) {
            wait_succeeded = atom->platform_semaphore()->WaitNoReset(0);
        } else {
            wait_succeeded = atom->platform_semaphore()->Wait(0);
        }

        if (wait_succeeded) {
            JobCompleted(atom->slot(), kArmMaliResultSuccess);
        } else {
            waiting_atoms_.push_back(atom);
            executing_atom_.reset();
            atom->platform_semaphore()->WaitAsync(owner_->GetPlatformPort());
        }
    } else {
        DASSERT(false);
    }
}
