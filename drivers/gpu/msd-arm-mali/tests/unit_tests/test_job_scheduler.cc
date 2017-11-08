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

class TestAddressSpaceObserver : public AddressSpaceObserver {
public:
    void FlushAddressMappingRange(AddressSpace*, uint64_t start, uint64_t length) override {}
    void ReleaseSpaceMappings(AddressSpace* address_space) override {}
};

class TestConnectionOwner : public MsdArmConnection::Owner {
public:
    void ScheduleAtom(std::shared_ptr<MsdArmAtom> atom) override {}
    AddressSpaceObserver* GetAddressSpaceObserver() override { return &address_space_observer_; }

private:
    TestAddressSpaceObserver address_space_observer_;
};
}

class TestJobScheduler {
public:
    void TestRunBasic()
    {
        TestOwner owner;
        TestConnectionOwner connection_owner;
        std::shared_ptr<MsdArmConnection> connection =
            MsdArmConnection::Create(0, &connection_owner);
        EXPECT_EQ(0u, owner.run_list().size());
        JobScheduler scheduler(&owner, 1);
        auto atom1 = std::make_unique<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());
        MsdArmAtom* atom1_ptr = atom1.get();
        scheduler.EnqueueAtom(std::move(atom1));
        EXPECT_EQ(0u, owner.run_list().size());

        auto atom2 = std::make_unique<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());
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

    void TestCancelJob()
    {
        TestOwner owner;
        TestConnectionOwner connection_owner;
        std::shared_ptr<MsdArmConnection> connection =
            MsdArmConnection::Create(0, &connection_owner);
        JobScheduler scheduler(&owner, 1);

        auto atom1 = std::make_shared<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(std::move(atom1));

        auto atom2 = std::make_shared<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(std::move(atom2));

        // Neither is scheduled, so they should be canceled immediately.
        bool canceled = false;
        scheduler.CancelAtomsForConnection(connection, [&canceled]() { canceled = true; });
        EXPECT_TRUE(canceled);
        EXPECT_EQ(0u, owner.run_list().size());
        EXPECT_EQ(0u, scheduler.GetAtomListSize());

        atom1 = std::make_shared<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());
        MsdArmAtom* atom1_ptr = atom1.get();
        scheduler.EnqueueAtom(atom1);

        atom2 = std::make_shared<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(atom2);
        scheduler.TryToSchedule();

        EXPECT_EQ(1u, owner.run_list().size());
        EXPECT_EQ(atom1_ptr, owner.run_list()[0]);

        canceled = false;
        scheduler.CancelAtomsForConnection(connection, [&canceled]() { canceled = true; });
        EXPECT_FALSE(canceled);
        EXPECT_EQ(0u, scheduler.GetAtomListSize());
        EXPECT_EQ(atom1.get(), scheduler.executing_atom());
        scheduler.JobCompleted(0);
        EXPECT_TRUE(canceled);

        // The second atom should have been thrown away, and the first should be
        // removed due to completion.
        EXPECT_EQ(1u, owner.run_list().size());
        EXPECT_EQ(0u, scheduler.GetAtomListSize());
    }

    void TestJobDependencies()
    {
        TestOwner owner;
        TestConnectionOwner connection_owner;
        std::shared_ptr<MsdArmConnection> connection =
            MsdArmConnection::Create(0, &connection_owner);
        JobScheduler scheduler(&owner, 1);

        auto unqueued_atom1 =
            std::make_shared<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());

        auto unqueued_atom2 =
            std::make_shared<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());

        auto atom2 = std::make_shared<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());
        atom2->set_dependencies({unqueued_atom1});
        scheduler.EnqueueAtom(atom2);

        auto atom3 = std::make_shared<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(atom3);

        auto atom4 = std::make_shared<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());
        atom4->set_dependencies({atom3, unqueued_atom2});
        scheduler.EnqueueAtom(atom4);

        EXPECT_EQ(3u, scheduler.GetAtomListSize());
        EXPECT_EQ(nullptr, scheduler.executing_atom());

        scheduler.TryToSchedule();

        // atom3 is the only one with no dependencies.
        EXPECT_EQ(atom3.get(), scheduler.executing_atom());
        EXPECT_EQ(2u, scheduler.GetAtomListSize());

        scheduler.JobCompleted(0);
        EXPECT_EQ(nullptr, scheduler.executing_atom());
        EXPECT_EQ(2u, scheduler.GetAtomListSize());

        atom3->set_finished();
        scheduler.TryToSchedule();

        // one dependency of atom2 isn't finished yet
        EXPECT_EQ(nullptr, scheduler.executing_atom());
        EXPECT_EQ(2u, scheduler.GetAtomListSize());

        unqueued_atom2->set_finished();
        scheduler.TryToSchedule();

        EXPECT_EQ(atom4.get(), scheduler.executing_atom());
        EXPECT_EQ(1u, scheduler.GetAtomListSize());

        unqueued_atom1.reset();

        scheduler.JobCompleted(0);
        EXPECT_EQ(atom2.get(), scheduler.executing_atom());
        EXPECT_EQ(0u, scheduler.GetAtomListSize());
    }
};

TEST(JobScheduler, RunBasic) { TestJobScheduler().TestRunBasic(); }

TEST(JobScheduler, CancelJob) { TestJobScheduler().TestCancelJob(); }

TEST(JobScheduler, JobDependencies) { TestJobScheduler().TestJobDependencies(); }
