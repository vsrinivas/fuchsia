#pragma once

#include <stdint.h>

#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/io_port_dispatcher.h>

#include <utils/ref_counted.h>
#include <utils/ref_ptr.h>

class UserThread;
class ProcessDispatcher;

class ExceptionPort : public mxtl::RefCounted<ExceptionPort> {
public:
    static mx_status_t Create(mxtl::RefPtr<IOPortDispatcher> io_port, uint64_t io_port_key,
                              mxtl::RefPtr<ExceptionPort>* eport);
    ~ExceptionPort();

    mx_status_t SendReport(const mx_exception_report_t* packet);

    void OnProcessExit(ProcessDispatcher* process);
    void OnThreadExit(UserThread* thread);

private:
    ExceptionPort(mxtl::RefPtr<IOPortDispatcher> io_port, uint64_t io_port_key);

    ExceptionPort(const ExceptionPort&) = delete;
    ExceptionPort& operator=(const ExceptionPort&) = delete;

    void OnDestruction();

    IOP_Packet* MakePacket(uint64_t key, const mx_exception_report_t* report, mx_size_t size);

    void BuildProcessGoneReport(mx_exception_report_t* report, mx_koid_t pid);
    void BuildThreadGoneReport(mx_exception_report_t* report, mx_koid_t pid, mx_koid_t tid);

    // These aren't locked as once the exception port is created these are
    // immutable (the io port itself has its own locking though).
    mxtl::RefPtr<IOPortDispatcher> io_port_;
    const uint64_t io_port_key_;
};
