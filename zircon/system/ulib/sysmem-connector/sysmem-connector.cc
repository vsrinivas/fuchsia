// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sysmem-connector/sysmem-connector.h>

#include <fbl/auto_lock.h>
#include <fbl/function.h>
#include <fbl/mutex.h>
#include <fbl/unique_fd.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/watcher.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/channel.h>

#include <fcntl.h>
#include <errno.h>
#include <queue>
#include <stdio.h>
#include <threads.h>

// The actual sysmem FIDL server is in the sysmem driver.  The code that watches
// for the driver and sends sysmem service requests to the driver is in
// ulib/sysmem-connector.  The code here just needs to queue requests to
// sysmem-connector.

// Clients interact with sysmem-connector using a C ABI, but we want to use C++
// for the implementation.  We have SysmemConnector inherit from an empty
// sysmem_connector struct just to make the SysmemConnector class officially be
// a class not a struct, and to make the SysmemConnector name consistent with
// C++ coding conventions, and have member names with "_" at the end consistent
// with coding conventions, etc.
//
// Every instance of sysmem_connector is actually a SysmemConnector and vice
// versa.
struct sysmem_connector {
  // Intentionally declared as empty; never instantiated; see SysmemConnector.
};
class SysmemConnector : public sysmem_connector {
  // public in this case just means public to this file.  The interface is via
  // the functions declared in lib/sysmem-connector/sysmem-connector.h.
 public:
  SysmemConnector(const char* sysmem_device_path);
  zx_status_t Start();
  void QueueRequest(zx::channel allocator_request);
  void Stop();

 private:
  void Post(fbl::Closure to_run);

  static zx_status_t DeviceAddedShim(int dirfd, int event, const char* fn, void* cookie);
  zx_status_t DeviceAdded(int dirfd, int event, const char* fn);

  zx_status_t ConnectToSysmemDriver();

  void ProcessQueue();

  //
  // Set once during construction + Start(), never set again.
  //

  const char* sysmem_device_path_{};
  async::Loop process_queue_loop_;
  thrd_t process_queue_thrd_{};

  //
  // Only touched from process_queue_loop_'s one thread.
  //

  fbl::unique_fd sysmem_dir_fd_;
  zx::channel driver_connector_client_;

  //
  // Synchronized using lock_.
  //

  fbl::Mutex lock_;
  std::queue<zx::channel> connection_requests_ __TA_GUARDED(lock_);
};

SysmemConnector::SysmemConnector(const char* sysmem_device_path)
    : sysmem_device_path_(sysmem_device_path),
      process_queue_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  ZX_DEBUG_ASSERT(sysmem_device_path_);
}

zx_status_t SysmemConnector::Start() {
  // The process_queue_thrd_ is filled out before any code that checks it runs on the thread,
  // because the current thread is the only thread that triggers anything to happen on the thread
  // being created here, and that is only done after process_queue_thrd_ is filled out by the
  // current thread.
  zx_status_t status =
      process_queue_loop_.StartThread("SysmemConnector-ProcessQueue", &process_queue_thrd_);
  if (status != ZX_OK) {
    printf("sysmem-connector: process_queue_loop_.StartThread() failed - status: %d\n", status);
    return status;
  }
  // Establish initial connection to sysmem driver async.
  Post([this] { ConnectToSysmemDriver(); });
  return ZX_OK;
}

void SysmemConnector::QueueRequest(zx::channel allocator_request) {
  ZX_DEBUG_ASSERT(thrd_current() != process_queue_thrd_);
  bool trigger_needed;
  {  // scope lock
    fbl::AutoLock lock(&lock_);
    trigger_needed = connection_requests_.empty();
    connection_requests_.emplace(std::move(allocator_request));
  }  // ~lock
  if (trigger_needed) {
    Post([this] { ProcessQueue(); });
  }
}

void SysmemConnector::Stop() {
  ZX_DEBUG_ASSERT(thrd_current() != process_queue_thrd_);
  process_queue_loop_.Quit();
  process_queue_loop_.JoinThreads();
  process_queue_loop_.Shutdown();
}

void SysmemConnector::Post(fbl::Closure to_run) {
  zx_status_t post_status = async::PostTask(process_queue_loop_.dispatcher(), std::move(to_run));
  // We don't expect this post to ever fail.
  ZX_ASSERT(post_status == ZX_OK);
}

zx_status_t SysmemConnector::DeviceAddedShim(int dirfd, int event, const char* fn, void* cookie) {
  ZX_DEBUG_ASSERT(cookie);
  SysmemConnector* connector = static_cast<SysmemConnector*>(cookie);
  return connector->DeviceAdded(dirfd, event, fn);
}

zx_status_t SysmemConnector::DeviceAdded(int dirfd, int event, const char* filename) {
  ZX_DEBUG_ASSERT(thrd_current() == process_queue_thrd_);
  if (event != WATCH_EVENT_ADD_FILE) {
    // Keep going on IDLE or REMOVE.  There's nothing else useful that the
    // current thread can do until a sysmem device instance is available,
    // and we don't have any reason to attempt to directly handle any
    // REMOVE(s) since we'll do fdio_watch_directory() again later from
    // scratch instead.
    return ZX_OK;
  }
  ZX_DEBUG_ASSERT(event == WATCH_EVENT_ADD_FILE);

  zx::channel driver_connector_client;
  zx::channel driver_connector_server;
  zx_status_t status = zx::channel::create(0, &driver_connector_client, &driver_connector_server);
  if (status != ZX_OK) {
    printf("SysmemConnector::DeviceAdded() zx::channel::create() failed - status: %d\n", status);
    ZX_DEBUG_ASSERT(status != ZX_ERR_STOP);
    // If channel create fails, give up on this attempt to find a sysmem
    // device instance.  If another request arrives later, we'll try again
    // later.
    return status;
  }

  // We don't intend to close dirfd; all paths should release not close.
  fbl::unique_fd unique_dirfd(dirfd);
  fdio_cpp::FdioCaller caller(std::move(unique_dirfd));
  status =
      fdio_service_connect_at(caller.borrow_channel(), filename, driver_connector_server.release());
  // Never close dirfd.
  caller.release().release();
  if (status != ZX_OK) {
    printf("SysmemConnector::DeviceAdded() fdio_service_connect_at() failed - status: %d\n",
           status);
    ZX_DEBUG_ASSERT(status != ZX_ERR_STOP);
    // If somehow fdio_service_connect_at() fails for this device instance,
    // keep watching for another device instance.
    return ZX_OK;
  }

  driver_connector_client_ = std::move(driver_connector_client);
  ZX_DEBUG_ASSERT(driver_connector_client_);
  return ZX_ERR_STOP;
}

zx_status_t SysmemConnector::ConnectToSysmemDriver() {
  ZX_DEBUG_ASSERT(thrd_current() == process_queue_thrd_);
  ZX_DEBUG_ASSERT(!driver_connector_client_);

  ZX_DEBUG_ASSERT(!sysmem_dir_fd_);
  {
    int fd = open(sysmem_device_path_, O_DIRECTORY | O_RDONLY);
    if (fd < 0) {
      printf("sysmem-connector: Failed to open %s: %d\n", sysmem_device_path_, errno);
      return ZX_ERR_INTERNAL;
    }
    sysmem_dir_fd_.reset(fd);
  }
  ZX_DEBUG_ASSERT(sysmem_dir_fd_);

  // Returns ZX_ERR_STOP as soon as one of the 000, 001 device instances is
  // found.  We rely on those to go away if the corresponding sysmem instance
  // is no longer operational, so that we don't find them when we call
  // ConnectToSysmemDriver() again upon discovering that we can't send to a
  // previous device instance.
  //
  // TODO(dustingreen): Currently if this watch never finds a sysmem device
  // instance, then sysmem_connector_release() will block forever.  This can
  // be fixed once it's feasible to use DeviceWatcher (or similar) here
  // instead (currently DeviceWatcher is in garnet not zircon).
  zx_status_t watch_status =
      fdio_watch_directory(sysmem_dir_fd_.get(), DeviceAddedShim, ZX_TIME_INFINITE, this);
  if (watch_status != ZX_ERR_STOP) {
    printf("sysmem-connector: Failed to find sysmem device - status: %d\n", watch_status);
    return watch_status;
  }
  ZX_DEBUG_ASSERT(driver_connector_client_);
  return ZX_OK;
}

void SysmemConnector::ProcessQueue() {
  ZX_DEBUG_ASSERT(thrd_current() == process_queue_thrd_);
  while (true) {
    zx::channel allocator_request;
    {  // scope lock
      fbl::AutoLock lock(&lock_);
      if (connection_requests_.empty()) {
        return;
      }
      allocator_request = std::move(connection_requests_.front());
      connection_requests_.pop();
    }  // ~lock
    ZX_DEBUG_ASSERT(allocator_request);

    // Poll for PEER_CLOSED just before we need the channel to be usable, to
    // avoid routing a request to a stale no-longer-usable sysmem device
    // instance.  This doesn't eliminate the inherent race where a request
    // can be sent to an instance that's already started failing - that race
    // is fine.  This check is just a best-effort way to avoid routing to a
    // super-stale previous instance.
    //
    // TODO(dustingreen): When it becomes more convenient, switch to
    // noticing PEER_CLOSED async.  Currently it doesn't seem particularly
    // safe to use sysmem_fdio_caller_.borrow_channel() to borrow for an
    // async wait.
    if (driver_connector_client_) {
      zx_signals_t observed;
      zx_status_t wait_status = zx_object_wait_one(
          driver_connector_client_.get(), ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE_PAST, &observed);
      if (wait_status == ZX_OK) {
        ZX_DEBUG_ASSERT(observed & ZX_CHANNEL_PEER_CLOSED);
        // This way, we'll call ConnectToSysmemDriver() below.
        driver_connector_client_.reset();
      } else {
        // Any other failing status is unexpected.
        ZX_DEBUG_ASSERT(ZX_ERR_TIMED_OUT);
      }
    }

    if (!driver_connector_client_) {
      zx_status_t connect_status = ConnectToSysmemDriver();
      if (connect_status != ZX_OK) {
        // ~allocator_request - we'll try again to connect to a sysmem
        // instance next time a request comes in, but any given request
        // gets a max of one attempt to connect to a sysmem device
        // instance, in case attempts to find a sysmem device instance
        // are just failing.
        return;
      }
    }
    ZX_DEBUG_ASSERT(driver_connector_client_);

    zx_status_t send_connect_status = fuchsia_sysmem_DriverConnectorConnect(
        driver_connector_client_.get(), allocator_request.release());
    if (send_connect_status != ZX_OK) {
      // The most likely failing send_connect_status is
      // ZX_ERR_PEER_CLOSED, which can happen if the channel closed since
      // we checked above.  Since we don't really expect even
      // ZX_ERR_PEER_CLOSED unless sysmem is having problems, complain
      // about the error regardless of which error.
      printf(
          "SysmemConnector::ProcessQueue() DriverConnectorConnect() returned unexpected status: "
          "%d\n",
          send_connect_status);

      // Regardless of the specific error, we want to try
      // ConnectToSysmemDriver() again for the _next_ request.
      driver_connector_client_.reset();

      // We don't retry this request (the window for getting
      // ZX_ERR_PEER_CLOSED is short due to check above, and exists in any
      // case due to possibility of close from other end at any time), but
      // the next request will try ConnectToSysmemDriver() again.
      //
      // continue with next request
    }
  }
}

zx_status_t sysmem_connector_init(const char* sysmem_device_path,
                                  sysmem_connector_t** out_connector) {
  SysmemConnector* connector = new SysmemConnector(sysmem_device_path);
  zx_status_t status = connector->Start();
  if (status != ZX_OK) {
    printf("sysmem_connector_init() connector->Start() failed - status: %d\n", status);
    return status;
  }
  *out_connector = connector;
  return ZX_OK;
}

void sysmem_connector_queue_connection_request(sysmem_connector_t* connector_param,
                                               zx_handle_t allocator_request_param) {
  zx::channel allocator_request(allocator_request_param);
  ZX_DEBUG_ASSERT(connector_param);
  ZX_DEBUG_ASSERT(allocator_request);
  SysmemConnector* connector = static_cast<SysmemConnector*>(connector_param);
  connector->QueueRequest(std::move(allocator_request));
}

void sysmem_connector_release(sysmem_connector_t* connector_param) {
  ZX_DEBUG_ASSERT(connector_param);
  SysmemConnector* connector = static_cast<SysmemConnector*>(connector_param);
  connector->Stop();
  delete connector;
}
