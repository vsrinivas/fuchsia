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
    if (executing_atom_)
        return;
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

    if (executing_atom_)
        owner_->RunAtom(executing_atom_.get());
}

void JobScheduler::CancelAtomsForConnection(std::shared_ptr<MsdArmConnection> connection,
                                            std::function<void()> finished)
{
    DASSERT(connection);
    if (atoms_.empty()) {
        finished();
        return;
    }

    auto it = atoms_.begin();
    while (it != atoms_.end()) {
        if ((*it)->connection().lock() == connection)
            it = atoms_.erase(it);
        else
            ++it;
    }

    if (!executing_atom_ || executing_atom_->connection().lock() != connection) {
        finished();
        return;
    }

    finished_callbacks_.push_back(finished);
}

void JobScheduler::JobCompleted(uint64_t slot)
{
    // Ignore slot, because only one job can be running at a time.
    DASSERT(executing_atom_);
    owner_->AtomCompleted(executing_atom_.get());
    executing_atom_.reset();
    for (auto x : finished_callbacks_) {
        x();
    }
    finished_callbacks_.clear();
    TryToSchedule();
}

size_t JobScheduler::GetAtomListSize() { return atoms_.size(); }
