// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "job_scheduler.h"

JobScheduler::JobScheduler(Owner* owner, uint32_t job_slots) : owner_(owner), job_slots_(job_slots)
{
}

void JobScheduler::EnqueueAtom(std::unique_ptr<MsdArmAtom> atom)
{
    atoms_.push_back(std::move(atom));
}

void JobScheduler::TryToSchedule()
{
    if (running_)
        return;
    if (atoms_.empty())
        return;
    running_ = true;
    owner_->RunAtom(atoms_.front().get());
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
    if (running_)
        it++;
    while (it != atoms_.end()) {
        if ((*it)->connection().lock() == connection)
            it = atoms_.erase(it);
        else
            ++it;
    }

    if (atoms_.empty() || atoms_.front()->connection().lock() != connection) {
        finished();
        return;
    }

    finished_callbacks_.push_back(finished);
}

void JobScheduler::JobCompleted(uint64_t slot)
{
    // Ignore slot, because only one job can be running at a time.
    DASSERT(running_);
    DASSERT(!atoms_.empty());
    running_ = false;
    owner_->AtomCompleted(atoms_.front().get());
    atoms_.pop_front();
    for (auto x : finished_callbacks_) {
        x();
    }
    finished_callbacks_.clear();
    TryToSchedule();
}

size_t JobScheduler::GetAtomListSize() { return atoms_.size(); }
