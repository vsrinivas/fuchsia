// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "job_scheduler.h"

#include "magma_util/dlog.h"
#include "msd_arm_connection.h"
#include "msd_defs.h"
#include "platform_trace.h"

JobScheduler::JobScheduler(Owner* owner, uint32_t job_slots)
    : owner_(owner), job_slots_(job_slots), executing_atoms_(job_slots), runnable_atoms_(job_slots)
{
}

void JobScheduler::EnqueueAtom(std::shared_ptr<MsdArmAtom> atom)
{
    atoms_.push_back(std::move(atom));
}

// Use different names for different slots so they'll line up cleanly in the
// trace viewer.
static const char* AtomRunningString(uint32_t slot)
{
    switch (slot) {
        case 0:
            return "Atom running slot 0";
        case 1:
            return "Atom running slot 1";
        case 2:
            return "Atom running slot 2";
        default:
            DASSERT(false);
            return "Atom running unknown slot";
    }
}

void JobScheduler::MoveAtomsToRunnable()
{
    // Movement to next iterator happens inside loop.
    // Atoms can't depend on those after them, so one pass through the loop
    // should be enough.
    for (auto it = atoms_.begin(); it != atoms_.end();) {
        std::shared_ptr<MsdArmAtom> atom = *it;
        bool dependencies_finished;
        atom->UpdateDependencies(&dependencies_finished);
        if (dependencies_finished) {
            it = atoms_.erase(it);
            auto soft_atom = MsdArmSoftAtom::cast(atom);
            ArmMaliResultCode dep_status = atom->GetFinalDependencyResult();
            if (dep_status != kArmMaliResultSuccess) {
                owner_->AtomCompleted(atom.get(), dep_status);
            } else if (soft_atom) {
                soft_atom->SetExecutionStarted();
                ProcessSoftAtom(soft_atom);
            } else if (atom->IsDependencyOnly()) {
                owner_->AtomCompleted(atom.get(), kArmMaliResultSuccess);
            } else {
                DASSERT(atom->slot() < runnable_atoms_.size());
                runnable_atoms_[atom->slot()].push_back(atom);
            }
        } else {
            DLOG("Skipping atom %lx due to dependency", atom->gpu_address());
            ++it;
        }
    }
}

void JobScheduler::ScheduleRunnableAtoms()
{
    for (uint32_t slot = 0; slot < runnable_atoms_.size(); slot++) {
        if (executing_atoms_[slot]) {
            std::shared_ptr<MsdArmAtom> atom = executing_atoms_[slot];
            if (atom->soft_stopped()) {
                // No point trying to soft-stop an atom that's already stopping.
                continue;
            }
            auto& runnable = runnable_atoms_[slot];
            bool found_preempter = false;
            for (auto preempting = runnable.begin(); preempting != runnable.end(); ++preempting) {
                std::shared_ptr<MsdArmAtom> preempting_atom = *preempting;
                if (preempting_atom->connection().lock() == atom->connection().lock() &&
                    preempting_atom->priority() > atom->priority()) {
                    found_preempter = true;
                    break;
                }
            }
            if (found_preempter) {
                atom->set_soft_stopped(true);
                // If the atom's soft-stopped its current state will be saved in the job chain so it
                // will restart at the place it left off. When JobCompleted is received it will be
                // requeued so it can run again, priority permitting.
                owner_->SoftStopAtom(atom.get());
            }
        } else {
            auto& runnable = runnable_atoms_[slot];
            if (runnable.empty())
                continue;
            std::shared_ptr<MsdArmAtom> atom = runnable.front();
            DASSERT(!MsdArmSoftAtom::cast(atom));
            DASSERT(atom->GetFinalDependencyResult() == kArmMaliResultSuccess);
            DASSERT(!atom->IsDependencyOnly());
            DASSERT(atom->slot() == slot);

            for (auto preempting = std::next(runnable.begin()); preempting != runnable.end();
                 ++preempting) {
                std::shared_ptr<MsdArmAtom> preempting_atom = *preempting;
                if (preempting_atom->connection().lock() == atom->connection().lock() &&
                    preempting_atom->priority() > atom->priority()) {
                    // Swap the lower priority atom to the current location so we
                    // don't change the ratio of atoms executed between connections.
                    std::swap(atom, *preempting);
                    // Keep looping, as there may be an even higher priority
                    // atom.
                }
            }
            DASSERT(atom->slot() == slot);

            atom->SetExecutionStarted();
            executing_atoms_[slot] = atom;
            runnable.erase(runnable.begin());
            std::shared_ptr<MsdArmConnection> connection = atom->connection().lock();
            msd_client_id_t id = connection ? connection->client_id() : 0;
            TRACE_ASYNC_BEGIN("magma", AtomRunningString(slot),
                              executing_atoms_[slot]->trace_nonce(), "id", id);
            owner_->RunAtom(executing_atoms_[slot].get());
        }
    }
}

void JobScheduler::TryToSchedule()
{
    MoveAtomsToRunnable();
    ScheduleRunnableAtoms();

    UpdatePowerManager();
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
    for (auto& runnable_list : runnable_atoms_)
        runnable_list.remove_if(removal_function);
}

void JobScheduler::JobCompleted(uint64_t slot, ArmMaliResultCode result_code)
{
    std::shared_ptr<MsdArmAtom>& atom = executing_atoms_[slot];
    DASSERT(atom);
    TRACE_ASYNC_END("magma", AtomRunningString(slot), atom->trace_nonce());
    if (result_code == kArmMaliResultSoftStopped) {
        atom->set_soft_stopped(false);
        runnable_atoms_[slot].push_front(atom);
    }
    owner_->AtomCompleted(atom.get(), result_code);
    atom.reset();
    TryToSchedule();
}

void JobScheduler::SoftJobCompleted(std::shared_ptr<MsdArmSoftAtom> atom)
{
    owner_->AtomCompleted(atom.get(), kArmMaliResultSuccess);
    // The loop in TryToSchedule should cause any atoms that just had their
    // dependencies satisfied to run.
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
    auto timeout_time = Clock::time_point::max();
    for (auto& atom : executing_atoms_) {
        if (!atom || atom->hard_stopped())
            continue;
        auto atom_timeout_time =
            atom->execution_start_time() + std::chrono::milliseconds(timeout_duration_ms_);
        if (atom_timeout_time < timeout_time)
            timeout_time = atom_timeout_time;
    }

    for (auto& atom : waiting_atoms_) {
        auto atom_timeout_time = atom->execution_start_time() +
                                 std::chrono::milliseconds(semaphore_timeout_duration_ms_);
        if (atom_timeout_time < timeout_time)
            timeout_time = atom_timeout_time;
    }

    if (timeout_time == Clock::time_point::max())
        return Clock::duration::max();
    return timeout_time - Clock::now();
}

void JobScheduler::KillTimedOutAtoms()
{
    auto now = Clock::now();
    for (auto& atom : executing_atoms_) {
        if (!atom || atom->hard_stopped())
            continue;
        if (atom->execution_start_time() + std::chrono::milliseconds(timeout_duration_ms_) <= now) {
            atom->set_hard_stopped();
            owner_->HardStopAtom(atom.get());
        }
    }
    bool removed_waiting_atoms = false;
    for (auto it = waiting_atoms_.begin(); it != waiting_atoms_.end();) {
        std::shared_ptr<MsdArmAtom> atom = *it;
        auto atom_timeout_time = atom->execution_start_time() +
                                 std::chrono::milliseconds(semaphore_timeout_duration_ms_);
        if (atom_timeout_time <= now) {
            removed_waiting_atoms = true;
            owner_->AtomCompleted(atom.get(), kArmMaliResultTimedOut);
            // The semaphore wait on the port will be canceled by the closing of the event handle.
            it = waiting_atoms_.erase(it);
        } else {
            ++it;
        }
    }
    if (removed_waiting_atoms)
        TryToSchedule();
}

void JobScheduler::ProcessSoftAtom(std::shared_ptr<MsdArmSoftAtom> atom)
{
    DASSERT(owner_->GetPlatformPort());
    if (atom->soft_flags() == kAtomFlagSemaphoreSet) {
        atom->platform_semaphore()->Signal();
        SoftJobCompleted(atom);
    } else if (atom->soft_flags() == kAtomFlagSemaphoreReset) {
        atom->platform_semaphore()->Reset();
        SoftJobCompleted(atom);
    } else if ((atom->soft_flags() == kAtomFlagSemaphoreWait) ||
               (atom->soft_flags() == kAtomFlagSemaphoreWaitAndReset)) {
        bool wait_succeeded;
        if (atom->soft_flags() == kAtomFlagSemaphoreWait) {
            wait_succeeded = atom->platform_semaphore()->WaitNoReset(0);
        } else {
            wait_succeeded = atom->platform_semaphore()->Wait(0);
        }

        if (wait_succeeded) {
            SoftJobCompleted(atom);
        } else {
            waiting_atoms_.push_back(atom);
            atom->platform_semaphore()->WaitAsync(owner_->GetPlatformPort());
        }
    } else {
        DASSERT(false);
    }
}

void JobScheduler::ReleaseMappingsForConnection(std::shared_ptr<MsdArmConnection> connection)
{

    for (auto& executing_atom : executing_atoms_) {
        if (executing_atom && executing_atom->connection().lock() == connection) {
            executing_atom->set_hard_stopped();
            owner_->ReleaseMappingsForAtom(executing_atom.get());
        }
    }
}

void JobScheduler::UpdatePowerManager()
{
    bool active = false;
    for (std::shared_ptr<MsdArmAtom>& slot : executing_atoms_) {
        if (slot)
            active = true;
    }
    owner_->UpdateGpuActive(active);
}
