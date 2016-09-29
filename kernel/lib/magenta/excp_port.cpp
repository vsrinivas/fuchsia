#include <err.h>
#include <inttypes.h>
#include <new.h>
#include <string.h>

#include <magenta/excp_port.h>
#include <magenta/magenta.h>
#include <magenta/process_dispatcher.h>
#include <magenta/thread_dispatcher.h>
#include <magenta/user_thread.h>

#include <trace.h>

#define LOCAL_TRACE 0

// static
mx_status_t ExceptionPort::Create(mxtl::RefPtr<IOPortDispatcher> io_port, uint64_t io_port_key,
                                  mxtl::RefPtr<ExceptionPort>* out_eport) {
    AllocChecker ac;
    auto eport = new (&ac) ExceptionPort(mxtl::move(io_port), io_port_key);
    if (!ac.check())
        return ERR_NO_MEMORY;
    *out_eport = mxtl::AdoptRef<ExceptionPort>(eport);
    return NO_ERROR;
}

ExceptionPort::ExceptionPort(mxtl::RefPtr<IOPortDispatcher> io_port, uint64_t io_port_key)
    : io_port_(io_port), io_port_key_(io_port_key) {
    LTRACE_ENTRY_OBJ;
}

ExceptionPort::~ExceptionPort() {
    LTRACE_ENTRY_OBJ;
}

// This is called when the exception handler goes away.
// Its job is to uninstall the exception port and mark any threads waiting on the handler with
// MX_EXCEPTION_STATUS_HANDLER_GONE. One could use MX_EXCEPTION_STATUS_NOT_HANDLED, however it's not
// in order to add some clarity to the reason for the exception not being handled.
// TODO(dje): This isn't wired up yet.

void ExceptionPort::OnDestruction() {
    LTRACE_ENTRY_OBJ;
    // Note: "Gone" notifications aren't replied to. If there are any that
    // haven't been read yet then just discard them.
    // TODO(dje): Find threads blocked on the handler and unblock them.
    // TODO(dje): Remember this isn't wired up yet.
}

IOP_Packet* ExceptionPort::MakePacket(uint64_t key, const mx_exception_report_t* report, mx_size_t size) {
    auto pk = IOP_Packet::Alloc(size + sizeof(mx_packet_header_t));
    if (!pk)
        return nullptr;

    auto pkt_data = reinterpret_cast<mx_exception_packet_t*>(
        reinterpret_cast<char*>(pk) + sizeof(IOP_Packet));

    memcpy(&pkt_data->report, report, size);
    pkt_data->hdr.key = key;
    pkt_data->hdr.type = MX_PORT_PKT_TYPE_EXCEPTION;
    pkt_data->hdr.extra = 0; // currently unused

    return pk;
}

mx_status_t ExceptionPort::SendReport(const mx_exception_report_t* report) {
    LTRACEF("Sending exception report, type %u, pid %"
            PRIu64 ", tid %" PRIu64 "\n",
            report->header.type, report->context.pid, report->context.tid);

    auto iopk = MakePacket(io_port_key_, report, sizeof(*report));
    if (!iopk)
        return ERR_NO_MEMORY;

    return io_port_->Queue(iopk);
}

void ExceptionPort::BuildProcessGoneReport(mx_exception_report_t* report,
                                           mx_koid_t pid) {
    memset(report, 0, sizeof(*report));
    report->header.size = sizeof(*report);
    report->header.type = MX_EXCP_GONE;
    report->context.pid = pid;
    report->context.tid = MX_KOID_INVALID;
}

void ExceptionPort::BuildThreadGoneReport(mx_exception_report_t* report,
                                          mx_koid_t pid, mx_koid_t tid) {
    memset(report, 0, sizeof(*report));
    report->header.size = sizeof(*report);
    report->header.type = MX_EXCP_GONE;
    report->context.pid = pid;
    report->context.tid = tid;
}

// This isn't called for every process's destruction, only for processes that
// have a process-specific exception handler.
// TODO(dje): Debugger's needs.

void ExceptionPort::OnProcessExit(ProcessDispatcher* process) {
    mx_koid_t pid = process->get_koid();
    LTRACEF("process %" PRIu64 " gone\n", pid);
    mx_exception_report_t report;
    BuildProcessGoneReport(&report, pid);
    // The result is ignored, not much else we can do.
    SendReport(&report);
}

// This isn't called for every thread's destruction, only for threads that
// have a thread-specific exception handler.
// TODO(dje): Debugger's needs.

void ExceptionPort::OnThreadExit(UserThread* thread) {
    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " gone\n", pid, tid);
    mx_exception_report_t report;
    BuildThreadGoneReport(&report, pid, tid);
    // The result is ignored, not much else we can do.
    SendReport(&report);
}
