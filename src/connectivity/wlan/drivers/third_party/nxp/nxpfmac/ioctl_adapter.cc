// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"

#include <lib/async/cpp/task.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device.h"

namespace wlan::nxpfmac {

constexpr char kIoctlWorkerThreadName[] = "nxpfmac_ioctl_worker";

zx::result<std::unique_ptr<IoctlAdapter>> IoctlAdapter::Create(void* mlan_adapter,
                                                               BusInterface* bus) {
  std::unique_ptr<IoctlAdapter> adapter(new IoctlAdapter(mlan_adapter, bus));

  const zx_status_t status = adapter->loop_.StartThread(kIoctlWorkerThreadName);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to start ioctl worker thread: %s", zx_status_get_string(status));
    return zx::error(status);
  }

  return zx::success(std::move(adapter));
}

IoctlAdapter::IoctlAdapter(void* mlan_adapter, BusInterface* bus)
    : loop_(&kAsyncLoopConfigNeverAttachToThread), mlan_adapter_(mlan_adapter), bus_(bus) {}

IoctlAdapter::~IoctlAdapter() {
  loop_.Quit();
  loop_.JoinThreads();
}

void IoctlAdapter::OnIoctlComplete(mlan_ioctl_req* request, IoctlStatus status) {
  auto ioctl_request = reinterpret_cast<IoctlRequest<nullptr_t>*>(request);

  if (!ioctl_request->callback_) {
    return;
  }

  // Run the ioctl complete on its own thread. This ensures that the IRQ thread loop, which is the
  // thread processing these events, is not blocked by processing completed ioctls. This also allows
  // additional ioctl requests in the ioctl complete callback, they would otherwise block and cause
  // a deadlock because the IRQ main process holds the necessary locks. It also provides predictable
  // sequencing for timeout handling.
  async::PostTask(loop_.dispatcher(), [=]() {
    IoctlStatus callback_status =
        request->status_code == MLAN_ERROR_CMD_CANCEL ? IoctlStatus::Canceled : status;
    if (ioctl_request->timeout_task_) {
      if (ioctl_request->timeout_task_->timed_out.has_value()) {
        // The timeout has fired, at this point we cannot cancel it, it's not there anymore. Just
        // because the timeout fired doesn't mean it necessarily timed out though. It's possible
        // that the ioctl completed but that the completion was scheduled after the timeout. In that
        // case we return the status from the actual ioctl instead of IoctlStatus::Timeout.
        if (ioctl_request->timeout_task_->timed_out.value()) {
          callback_status = IoctlStatus::Timeout;
        }
      } else {
        // The task has an associated timeout that has not yet fired. Cancel it.
        const zx_status_t status =
            async_cancel_task(ioctl_request->timeout_task_->ioctl_adapter->loop_.dispatcher(),
                              ioctl_request->timeout_task_.get());
        // This call has to succeed, otherwise the timeout callback has not properly set the
        // timed_out flag.
        ZX_ASSERT(status == ZX_OK);
      }
      // Delete the timeout task. It was either canceled or has already run. It should not stay
      // around since that could interfere with future request with the same request object.
      ioctl_request->timeout_task_.reset();
    }
    // Swap out the callback in the request to a local copy. This clears out the callback in the
    // request so that it can safely re-assign to it. It also ensures that the callback object stays
    // alive for the duration of its use. Otherwise it might be deleted if something in the callback
    // reuses the same request object and assigns a new callback to it.
    std::shared_ptr<IoctlRequestCallback> callback;
    std::swap(callback, ioctl_request->callback_);
    if (callback) {
      (*callback)(&ioctl_request->IoctlReq(), callback_status);
    }
    // Now let the callback pointer expire, destroying it and any captures it might hold.
  });
}

IoctlStatus IoctlAdapter::IssueIoctl(mlan_ioctl_req* request, zx_duration_t timeout) {
  auto ioctl_request = reinterpret_cast<IoctlRequest<nullptr_t>*>(request);
  if (request->action != MLAN_ACT_CANCEL) {
    // Unless a request is being canceled the request must not have an existing timeout_task_, this
    // would indicate either reuse of the request while it's in progress or a bug in the timeout
    // handling code on ioctl completion.
    ZX_ASSERT(!ioctl_request->timeout_task_);
  }

  const zx_time_t deadline = timeout == ZX_TIME_INFINITE ? timeout : zx_deadline_after(timeout);

  std::weak_ptr<IoctlRequestCallback> weak_callback(ioctl_request->callback_);

  const mlan_status ml_status = mlan_ioctl(mlan_adapter_, request);
  if (ml_status == MLAN_STATUS_PENDING) {
    zx_status_t status = bus_->TriggerMainProcess();
    if (status != ZX_OK) {
      NXPF_ERR("Failed to trigger main process: %s", zx_status_get_string(status));
      return IoctlStatus::Failure;
    }
    if (deadline != ZX_TIME_INFINITE) {
      // Schedule the timeout task asynchronously so that we ensure that it's sequenced either
      // before or after the ioctl complete callback. Just in case the ioctl completes before we get
      // to this point.
      status = async::PostTask(loop_.dispatcher(),
                               [=]() { ScheduleTimeoutTask(request, weak_callback, deadline); });
      if (status != ZX_OK) {
        if (CancelIoctl(request)) {
          // Successfully canceled, indicate failure since we couldn't schedule the timeout task.
          return IoctlStatus::Failure;
        }
        // The cancelation didn't work, that means that it already ran the ioctl complete callback,
        // or is about to. We have to consider this a pending result. Since the ioctl completed
        // before the timeout handler was even able to start it should not be considered a timeout
        // anyway so it doesn't really matter that the timeout task couldn't start.
        return IoctlStatus::Pending;
      }
    }
    return IoctlStatus::Pending;
  }
  if (ml_status != MLAN_STATUS_SUCCESS) {
    NXPF_ERR("mlan_ioctl failed: %d", ml_status);
    return IoctlStatus::Failure;
  }
  return IoctlStatus::Success;
}

IoctlStatus IoctlAdapter::IssueIoctlSync(mlan_ioctl_req* request, zx_duration_t timeout) {
  auto ioctl_request = reinterpret_cast<IoctlRequest<nullptr_t>*>(request);
  sync_completion_t completion;
  IoctlStatus ioctl_status = IoctlStatus::Failure;
  ioctl_request->callback_ =
      std::make_shared<IoctlRequestCallback>([&](pmlan_ioctl_req req, IoctlStatus status) {
        ioctl_status = status;
        sync_completion_signal(&completion);
      });

  // Perform this ioctl with an infinite timeout, the timeout is instead used when waiting for
  // the completion.
  const IoctlStatus status = IssueIoctl(request, ZX_TIME_INFINITE);
  if (status == IoctlStatus::Pending) {
    zx_status_t wait_status = sync_completion_wait(&completion, timeout);
    if (wait_status == ZX_ERR_TIMED_OUT) {
      if (CancelIoctl(request)) {
        // We successfully canceled something, this should call our callback with a canceled status.
        // So we need to wait for the completion again, otherwise the callback might get destroyed
        // before it's called.
        wait_status = sync_completion_wait(&completion, ZX_TIME_INFINITE);
        if (wait_status != ZX_OK) {
          NXPF_ERR("Failed to wait for ioctl cancelation: %s", zx_status_get_string(wait_status));
        }
      }
      return IoctlStatus::Timeout;
    }
    return ioctl_status;
  }
  return status;
}

bool IoctlAdapter::CancelIoctl(mlan_ioctl_req* request) {
  request->action = MLAN_ACT_CANCEL;
  // Canceling an ioctl always returns success, not point in checking.
  IssueIoctl(request, ZX_TIME_INFINITE);
  return request->status_code == MLAN_ERROR_CMD_CANCEL;
}

void IoctlAdapter::CancelAllIoctls() {
  // This operation always returns success so no point in checking the result.
  mlan_ioctl(mlan_adapter_, nullptr);
}

void IoctlAdapter::ScheduleTimeoutTask(mlan_ioctl_req* request,
                                       const std::weak_ptr<IoctlRequestCallback>& weak_callback,
                                       zx_time_t deadline) {
  using TimeoutTask = IoctlRequest<nullptr_t>::TimeoutTask;
  auto ioctl_request = reinterpret_cast<IoctlRequest<nullptr_t>*>(request);

  auto callback = weak_callback.lock();

  if (!callback) {
    // The ioctl already completed and removed the callback. No need for a timeout task.
    return;
  }

  auto on_timeout = [](async_dispatcher_t*, async_task_t* task, zx_status_t status) {
    if (status != ZX_OK) {
      if (status != ZX_ERR_CANCELED) {
        // Anything but canceled is unexpected and should be logged.
        NXPF_ERR("Timeout task failed: %s", zx_status_get_string(status));
      }
      return;
    }
    auto timeout_task = static_cast<TimeoutTask*>(task);
    auto ioctl_request = reinterpret_cast<IoctlRequest<nullptr_t>*>(timeout_task->request);

    // It's important to set this before calling CancelIoctl so that if the cancelation below works
    // it will pick up on this state in the ioctl complete callback.
    timeout_task->timed_out = true;

    // Now cancel the task. Since the completion of the ioctl and this timeout handler run on the
    // same, single thread they will be sequenced so that one happens after the other. If we
    // are able to cancel the task here then the ioctl complete call (called by mlan upon
    // cancelation) will happen after this and should indicate a timeout. If we fail to cancel the
    // ioctl here it means that the ioctl has completed but its complete call has been scheduled
    // after this timeout callback. In that case we don't do anything, the ioctl has completed and
    // the complete callback will be notified of the result, there is no timeout.
    if (!timeout_task->ioctl_adapter->CancelIoctl(ioctl_request)) {
      // The ioctl could not be canceled. At this point the ioctl completion will be called because
      // the ioctl is in progress. When this happens we must not indicate timeout since the ioctl
      // actually already ran. Since the ioctl complete callback has not canceled this timeout task
      // it means that it hasn't run yet so it must not indicate timeout when it does.
      timeout_task->timed_out = false;
    }
  };

  ioctl_request->timeout_task_ = std::make_unique<TimeoutTask>(this, deadline, on_timeout, request);

  const zx_status_t status =
      async_post_task(loop_.dispatcher(), ioctl_request->timeout_task_.get());
  if (status != ZX_OK) {
    // We could not post the timeout task. Consider this a failure for the entire ioctl. Remove the
    // timeout task here to prevent the ioctl complete callback from indicating cancelation. It
    // should indicate failure.
    ioctl_request->timeout_task_.reset();
    // The outcome of the cancelation doesn't really matter. If the ioctl is canceled successfully
    // then the ioctl complete callback will be called because of the cancelation. If the
    // cancelation fails then the ioctl actually completed and it doesn't matter that we were unable
    // to post the timeout task.
    CancelIoctl(request);
    NXPF_ERR("Failed to post timeout task: %s", zx_status_get_string(status));
  }
}
}  // namespace wlan::nxpfmac
