// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "job_scheduler.h"

#include <utility>
#include <vector>

#include "msd_arm_connection.h"
#include "platform_port.h"
#include "gtest/gtest.h"

namespace {

class TestOwner : public JobScheduler::Owner {
public:
    using ResultPair = std::pair<MsdArmAtom*, ArmMaliResultCode>;

    TestOwner() { platform_port_ = magma::PlatformPort::Create(); }

    void RunAtom(MsdArmAtom* atom) override { run_list_.push_back(atom); }
    void AtomCompleted(MsdArmAtom* atom, ArmMaliResultCode result_code) override
    {
        completed_list_.push_back(ResultPair(atom, result_code));
    }
    void HardStopAtom(MsdArmAtom* atom) override { stopped_atoms_.push_back(atom); }
    magma::PlatformPort* GetPlatformPort() override { return platform_port_.get(); }

    std::vector<MsdArmAtom*>& run_list() { return run_list_; }
    std::vector<ResultPair>& completed_list() { return completed_list_; }
    std::vector<MsdArmAtom*>& stopped_atoms() { return stopped_atoms_; }

private:
    std::vector<MsdArmAtom*> run_list_;
    std::vector<ResultPair> completed_list_;
    std::vector<MsdArmAtom*> stopped_atoms_;
    std::unique_ptr<magma::PlatformPort> platform_port_;
};

class TestAddressSpaceObserver : public AddressSpaceObserver {
public:
    void FlushAddressMappingRange(AddressSpace*, uint64_t start, uint64_t length,
                                  bool synchronous) override
    {
    }
    void ReleaseSpaceMappings(AddressSpace* address_space) override {}
};

class TestConnectionOwner : public MsdArmConnection::Owner {
public:
    void ScheduleAtom(std::shared_ptr<MsdArmAtom> atom) override {}
    void CancelAtoms(std::shared_ptr<MsdArmConnection> connection) override {}
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
        scheduler.JobCompleted(0, kArmMaliResultSuccess);
        EXPECT_EQ(2u, owner.run_list().size());
        EXPECT_EQ(atom2_ptr, owner.run_list()[1]);
        scheduler.JobCompleted(0, kArmMaliResultSuccess);
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
        scheduler.CancelAtomsForConnection(connection);
        EXPECT_EQ(0u, owner.run_list().size());
        EXPECT_EQ(0u, scheduler.GetAtomListSize());

        auto semaphore =
            std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create());
        auto waiting_atom = std::make_shared<MsdArmSoftAtom>(
            connection, kAtomFlagSemaphoreWait, semaphore, 0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(waiting_atom);

        atom1 = std::make_shared<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());
        MsdArmAtom* atom1_ptr = atom1.get();
        scheduler.EnqueueAtom(atom1);

        atom2 = std::make_shared<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(atom2);
        scheduler.TryToSchedule();

        EXPECT_EQ(1u, owner.run_list().size());
        EXPECT_EQ(atom1_ptr, owner.run_list()[0]);
        EXPECT_EQ(1u, scheduler.waiting_atoms_.size());

        scheduler.CancelAtomsForConnection(connection);
        EXPECT_EQ(0u, scheduler.GetAtomListSize());
        EXPECT_EQ(0u, scheduler.waiting_atoms_.size());
        EXPECT_EQ(atom1.get(), scheduler.executing_atom());
        scheduler.JobCompleted(0, kArmMaliResultSuccess);

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

        scheduler.JobCompleted(0, kArmMaliResultSuccess);
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

        scheduler.JobCompleted(0, kArmMaliResultSuccess);
        EXPECT_EQ(atom2.get(), scheduler.executing_atom());
        EXPECT_EQ(0u, scheduler.GetAtomListSize());
    }

    void TestTimeout()
    {
        TestOwner owner;
        TestConnectionOwner connection_owner;
        std::shared_ptr<MsdArmConnection> connection =
            MsdArmConnection::Create(0, &connection_owner);
        JobScheduler scheduler(&owner, 1);

        // Make timeout lower so test runs faster.
        static constexpr uint64_t kTimeoutDurationMs = 10;
        scheduler.set_timeout_duration(kTimeoutDurationMs);

        auto atom = std::make_shared<MsdArmAtom>(connection, 0, 0, 0, magma_arm_mali_user_data());
        MsdArmAtom* atom_ptr = atom.get();
        scheduler.EnqueueAtom(atom);
        DASSERT(scheduler.GetCurrentTimeoutDuration() == JobScheduler::Clock::duration::max());

        scheduler.TryToSchedule();
        DASSERT(scheduler.GetCurrentTimeoutDuration() <=
                std::chrono::milliseconds(kTimeoutDurationMs));
        while (scheduler.GetCurrentTimeoutDuration() > JobScheduler::Clock::duration::zero())
            ;
        EXPECT_EQ(0u, owner.stopped_atoms().size());
        scheduler.KillTimedOutAtoms();
        EXPECT_EQ(1u, owner.stopped_atoms().size());
        EXPECT_EQ(atom_ptr, owner.stopped_atoms()[0]);
        EXPECT_EQ(atom_ptr, scheduler.executing_atom());

        // Second kill shouldn't do anything, since the atom has already been
        // stopped.
        EXPECT_EQ(scheduler.GetCurrentTimeoutDuration(), JobScheduler::Clock::duration::max());
        scheduler.KillTimedOutAtoms();
        EXPECT_EQ(1u, owner.stopped_atoms().size());

        scheduler.JobCompleted(0, kArmMaliResultSuccess);
        EXPECT_EQ(nullptr, scheduler.executing_atom());
        EXPECT_EQ(scheduler.GetCurrentTimeoutDuration(), JobScheduler::Clock::duration::max());
    }

    void TestSemaphores()
    {
        TestOwner owner;
        TestConnectionOwner connection_owner;
        std::shared_ptr<MsdArmConnection> connection =
            MsdArmConnection::Create(0, &connection_owner);
        JobScheduler scheduler(&owner, 1);

        auto semaphore =
            std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create());

        auto atom1 = std::make_shared<MsdArmSoftAtom>(connection, kAtomFlagSemaphoreWait, semaphore,
                                                      0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(atom1);

        scheduler.TryToSchedule();
        EXPECT_EQ(nullptr, scheduler.executing_atom());
        auto atom2 = std::make_shared<MsdArmSoftAtom>(connection, kAtomFlagSemaphoreWait, semaphore,
                                                      0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(atom2);

        scheduler.TryToSchedule();
        EXPECT_EQ(0u, scheduler.GetAtomListSize());
        EXPECT_EQ(nullptr, scheduler.executing_atom());
        EXPECT_EQ(0u, owner.completed_list().size());

        uint64_t key;
        EXPECT_EQ(MAGMA_STATUS_TIMED_OUT, owner.GetPlatformPort()->Wait(&key, 0).get());

        auto atom3 = std::make_shared<MsdArmSoftAtom>(connection, kAtomFlagSemaphoreSet, semaphore,
                                                      0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(atom3);
        scheduler.TryToSchedule();

        EXPECT_EQ(1u, owner.completed_list().size());

        // Port should currently be waiting on semaphore which was just
        // signaled.
        EXPECT_EQ(MAGMA_STATUS_OK, owner.GetPlatformPort()->Wait(&key, 0).get());
        EXPECT_EQ(key, semaphore->id());
        scheduler.PlatformPortSignaled(key);

        EXPECT_EQ(0u, owner.run_list().size());
        EXPECT_EQ(3u, owner.completed_list().size());
        EXPECT_EQ(true, semaphore->WaitNoReset(0u));

        // Semaphore was set, so atom should complete immediately.
        auto atom_already_set = std::make_shared<MsdArmSoftAtom>(
            connection, kAtomFlagSemaphoreWait, semaphore, 0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(atom_already_set);
        scheduler.TryToSchedule();
        EXPECT_EQ(4u, owner.completed_list().size());

        auto atom4 = std::make_shared<MsdArmSoftAtom>(connection, kAtomFlagSemaphoreReset,
                                                      semaphore, 0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(atom4);
        scheduler.TryToSchedule();

        EXPECT_EQ(false, semaphore->WaitNoReset(0u));
        EXPECT_EQ(5u, owner.completed_list().size());

        auto atom5 = std::make_shared<MsdArmSoftAtom>(connection, kAtomFlagSemaphoreWaitAndReset,
                                                      semaphore, 0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(atom5);
        scheduler.TryToSchedule();

        EXPECT_EQ(5u, owner.completed_list().size());
        semaphore->Signal();

        EXPECT_EQ(MAGMA_STATUS_OK, owner.GetPlatformPort()->Wait(&key, 0).get());
        scheduler.PlatformPortSignaled(key);

        EXPECT_EQ(6u, owner.completed_list().size());
        EXPECT_EQ(false, semaphore->WaitNoReset(0u));

        auto atom6 = std::make_shared<MsdArmSoftAtom>(connection, kAtomFlagSemaphoreWaitAndReset,
                                                      semaphore, 0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(atom6);
        scheduler.TryToSchedule();

        EXPECT_EQ(6u, owner.completed_list().size());

        while (MAGMA_STATUS_OK == owner.GetPlatformPort()->Wait(&key, 0).get())
            ;

        semaphore->Signal();
        EXPECT_EQ(MAGMA_STATUS_OK, owner.GetPlatformPort()->Wait(&key, 0).get());
        semaphore->Reset();

        scheduler.PlatformPortSignaled(key);

        // Semaphore should still be reregistered with port in
        // PlatformPortSignaled, because the Reset happened before
        // WaitAndReset processed it.
        semaphore->Signal();
        EXPECT_EQ(MAGMA_STATUS_OK, owner.GetPlatformPort()->Wait(&key, 0).get());

        EXPECT_EQ(6u, owner.completed_list().size());

        semaphore->Signal();
        // All atoms have completed, so port shouldn't be waiting on semaphore
        // anymore.
        EXPECT_EQ(MAGMA_STATUS_TIMED_OUT, owner.GetPlatformPort()->Wait(&key, 0).get());

        for (auto& completed : owner.completed_list()) {
            EXPECT_EQ(kArmMaliResultSuccess, completed.second);
        }
    }

    void TestCancelNull()
    {
        TestOwner owner;
        TestConnectionOwner connection_owner;
        std::shared_ptr<MsdArmConnection> connection =
            MsdArmConnection::Create(0, &connection_owner);
        JobScheduler scheduler(&owner, 1);

        auto semaphore =
            std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create());

        auto atom1 = std::make_shared<MsdArmSoftAtom>(connection, kAtomFlagSemaphoreWait, semaphore,
                                                      0, magma_arm_mali_user_data());
        scheduler.EnqueueAtom(atom1);
        scheduler.TryToSchedule();

        EXPECT_EQ(1u, scheduler.waiting_atoms_.size());

        // Even if the connection is now null, canceling it should remove the
        // dead atom.
        connection.reset();
        scheduler.CancelAtomsForConnection(connection);
        EXPECT_EQ(0u, scheduler.waiting_atoms_.size());
    }
};

TEST(JobScheduler, RunBasic) { TestJobScheduler().TestRunBasic(); }

TEST(JobScheduler, CancelJob) { TestJobScheduler().TestCancelJob(); }

TEST(JobScheduler, JobDependencies) { TestJobScheduler().TestJobDependencies(); }

TEST(JobScheduler, Timeout) { TestJobScheduler().TestTimeout(); }

TEST(JobScheduler, Semaphores) { TestJobScheduler().TestSemaphores(); }

TEST(JobScheduler, CancelNull) { TestJobScheduler().TestCancelNull(); }
