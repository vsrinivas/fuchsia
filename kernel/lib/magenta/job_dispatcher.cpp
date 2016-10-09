// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/job_dispatcher.h>

#include <err.h>
#include <new.h>

#include <kernel/auto_lock.h>

#include <magenta/process_dispatcher.h>

constexpr mx_rights_t kDefaultJobRights =
    MX_RIGHT_TRANSFER | MX_RIGHT_DUPLICATE | MX_RIGHT_READ | MX_RIGHT_WRITE;

class JobNode : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<JobNode>> {
public:
    static JobNode* Make(mxtl::RefPtr<JobDispatcher> job);
    static JobNode* Make(mxtl::RefPtr<ProcessDispatcher> process);

    virtual ~JobNode() {}

    // JobNode API:
    virtual bool is_process() const = 0;
    virtual void Kill() = 0;

protected:
    JobNode() {}
};

class JobNodeJob final : public JobNode {
public:
    JobNodeJob() = delete;
    JobNodeJob(mxtl::RefPtr<JobDispatcher> job) : job_(mxtl::move(job)) {}

    bool is_process() const final { return false; }
    void Kill();

private:
    mxtl::RefPtr<JobDispatcher> job_;
};

class JobNodeProc final : public JobNode {
public:
    JobNodeProc() = delete;
    JobNodeProc(mxtl::RefPtr<ProcessDispatcher> proc) : proc_(mxtl::move(proc)) {}

    bool is_process() const final { return true; }
    void Kill() final;

private:
    mxtl::RefPtr<ProcessDispatcher> proc_;
};

JobNode* JobNode::Make(mxtl::RefPtr<JobDispatcher> job) {
    AllocChecker ac;
    auto node = new (&ac) JobNodeJob(mxtl::move(job));
    if (!ac.check())
        return nullptr;
    return node;
}

JobNode* JobNode::Make(mxtl::RefPtr<ProcessDispatcher> process) {
    AllocChecker ac;
    auto node = new (&ac) JobNodeProc(mxtl::move(process));
    if (!ac.check())
        return nullptr;
    return node;
}

void JobNodeJob::Kill() {
    job_->Kill();
}

void JobNodeProc::Kill() {
    proc_->Kill();
}

mxtl::RefPtr<JobDispatcher> JobDispatcher::CreateRootJob() {
    AllocChecker ac;
    auto job = mxtl::AdoptRef(new (&ac) JobDispatcher(0u, nullptr));
    return ac.check() ? job  : nullptr;
}

status_t JobDispatcher::Create(uint32_t flags,
                               mxtl::RefPtr<JobDispatcher> parent,
                               mxtl::RefPtr<Dispatcher>* dispatcher,
                               mx_rights_t* rights) {
    AllocChecker ac;
    auto job = mxtl::AdoptRef(new (&ac) JobDispatcher(flags, parent.get()));
    if (!ac.check())
        return ERR_NO_MEMORY;

    auto status = parent->AddChildJob(job);
    if (status != NO_ERROR)
        return status;

    *rights = kDefaultJobRights;
    *dispatcher = mxtl::RefPtr<Dispatcher>(job.get());
    return NO_ERROR;
}

JobDispatcher::JobDispatcher(uint32_t flags, JobDispatcher* parent)
    : flags_(flags), parent_(parent) {
    state_tracker_.set_initial_signals_state(mx_signals_state_t{0, MX_SIGNAL_SIGNALED});
}

JobDispatcher::~JobDispatcher() {
}

void JobDispatcher::on_zero_handles() {
}

mx_status_t JobDispatcher::AddChildProcess(mxtl::RefPtr<ProcessDispatcher> process) {
    AutoLock lock(&lock_);

    auto node = JobNode::Make(mxtl::move(process));
    if (!node)
        return ERR_NO_MEMORY;
    children_.push_back(mxtl::unique_ptr<JobNode>(node));
    return NO_ERROR;
}

mx_status_t JobDispatcher::AddChildJob(mxtl::RefPtr<JobDispatcher> job) {
    AutoLock lock(&lock_);

    auto node = JobNode::Make(mxtl::move(job));
    if (!node)
        return ERR_NO_MEMORY;
    children_.push_back(mxtl::unique_ptr<JobNode>(node));
    return NO_ERROR;
}

void JobDispatcher::Kill() {
    AutoLock lock(&lock_);

    for (auto& node : children_) {
        node.Kill();
    }
}
