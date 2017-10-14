// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "job_scheduler.h"

#include <vector>

#include "msd_arm_connection.h"
#include "gtest/gtest.h"

namespace {

class TestOwner : public JobScheduler::Owner {
public:
    void RunAtom(MsdArmAtom* atom) override { run_list_.push_back(atom); }
    void AtomCompleted(MsdArmAtom* atom) override {}

    std::vector<MsdArmAtom*>& run_list() { return run_list_; }

private:
    std::vector<MsdArmAtom*> run_list_;
};

TEST(JobScheduler, RunBasic)
{
    TestOwner owner;
    std::shared_ptr<MsdArmConnection> connection =
        std::shared_ptr<MsdArmConnection>(MsdArmConnection::Create(0));
    EXPECT_EQ(0u, owner.run_list().size());
    JobScheduler scheduler(&owner, 1);
    auto atom1 = std::make_unique<MsdArmAtom>(connection, 0, 0, 0);
    MsdArmAtom* atom1_ptr = atom1.get();
    scheduler.EnqueueAtom(std::move(atom1));
    EXPECT_EQ(0u, owner.run_list().size());

    auto atom2 = std::make_unique<MsdArmAtom>(connection, 0, 0, 0);
    MsdArmAtom* atom2_ptr = atom2.get();
    scheduler.EnqueueAtom(std::move(atom2));
    EXPECT_EQ(0u, owner.run_list().size());

    scheduler.TryToSchedule();
    EXPECT_EQ(1u, owner.run_list().size());
    EXPECT_EQ(atom1_ptr, owner.run_list()[0]);
    scheduler.JobCompleted(0);
    EXPECT_EQ(2u, owner.run_list().size());
    EXPECT_EQ(atom2_ptr, owner.run_list()[1]);
    scheduler.JobCompleted(0);
}

TEST(JobScheduler, CancelJob)
{
    TestOwner owner;
    std::shared_ptr<MsdArmConnection> connection =
        std::shared_ptr<MsdArmConnection>(MsdArmConnection::Create(0));
    JobScheduler scheduler(&owner, 1);

    auto atom1 = std::make_unique<MsdArmAtom>(connection, 0, 0, 0);
    scheduler.EnqueueAtom(std::move(atom1));

    auto atom2 = std::make_unique<MsdArmAtom>(connection, 0, 0, 0);
    scheduler.EnqueueAtom(std::move(atom2));

    // Neither is scheduled, so they should be canceled immediately.
    bool canceled = false;
    scheduler.CancelAtomsForConnection(connection, [&canceled]() { canceled = true; });
    EXPECT_TRUE(canceled);
    EXPECT_EQ(0u, owner.run_list().size());
    EXPECT_EQ(0u, scheduler.GetAtomListSize());

    atom1 = std::make_unique<MsdArmAtom>(connection, 0, 0, 0);
    MsdArmAtom* atom1_ptr = atom1.get();
    scheduler.EnqueueAtom(std::move(atom1));

    atom2 = std::make_unique<MsdArmAtom>(connection, 0, 0, 0);
    scheduler.EnqueueAtom(std::move(atom2));
    scheduler.TryToSchedule();

    EXPECT_EQ(1u, owner.run_list().size());
    EXPECT_EQ(atom1_ptr, owner.run_list()[0]);

    canceled = false;
    scheduler.CancelAtomsForConnection(connection, [&canceled]() { canceled = true; });
    EXPECT_FALSE(canceled);
    EXPECT_EQ(1u, scheduler.GetAtomListSize());
    scheduler.JobCompleted(0);
    EXPECT_TRUE(canceled);

    // The second atom should have been thrown away, and the first should be
    // removed due to completion.
    EXPECT_EQ(1u, owner.run_list().size());
    EXPECT_EQ(0u, scheduler.GetAtomListSize());
}
}
