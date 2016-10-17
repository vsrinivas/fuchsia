#pragma once

#include <stdint.h>

#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/syscalls/port.h>

#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>

class UserThread;
class ProcessDispatcher;
class PortDispatcher;

class ExceptionPort : public mxtl::RefCounted<ExceptionPort> {
public:
    static mx_status_t Create(mxtl::RefPtr<PortDispatcher> port, uint64_t port_key,
                              mxtl::RefPtr<ExceptionPort>* eport);
    ~ExceptionPort();

    mx_status_t SendReport(const mx_exception_report_t* packet);

    void OnProcessExit(ProcessDispatcher* process);
    void OnThreadExit(UserThread* thread);

private:
    ExceptionPort(mxtl::RefPtr<PortDispatcher> port, uint64_t port_key);

    ExceptionPort(const ExceptionPort&) = delete;
    ExceptionPort& operator=(const ExceptionPort&) = delete;

    void OnDestruction();

    void BuildProcessGoneReport(mx_exception_report_t* report, mx_koid_t pid);
    void BuildThreadGoneReport(mx_exception_report_t* report, mx_koid_t pid, mx_koid_t tid);

    // These aren't locked as once the exception port is created these are
    // immutable (the io port itself has its own locking though).
    mxtl::RefPtr<PortDispatcher> port_;
    const uint64_t port_key_;
};
