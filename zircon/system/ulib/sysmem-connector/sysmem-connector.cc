// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fit/function.h>
#include <lib/sysmem-connector/sysmem-connector.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <stdio.h>
#include <threads.h>

#include <queue>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_fd.h>

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
  SysmemConnector(const char* sysmem_directory_path, bool terminate_on_sysmem_connection_failure);
  zx_status_t Start();

  using QueueItem = std::variant<fidl::ServerEnd<fuchsia_sysmem::Allocator>,
                                 fidl::ClientEnd<fuchsia_io::Directory>>;

  void Queue(QueueItem&& queue_item);
  void Stop();

 private:
  void Post(fit::closure to_run);

  zx_status_t DeviceAdded(int dirfd, int event, const char* filename);

  zx_status_t ConnectToSysmemDriver();

  void ProcessQueue();

  void OnSysmemPeerClosed(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal_t* signal);

  //
  // Set once during construction + Start(), never set again.
  //

  // directory of device instances
  const char* sysmem_directory_path_{};
  async::Loop process_queue_loop_;
  thrd_t process_queue_thrd_{};
  bool terminate_on_sysmem_connection_failure_ = false;

  //
  // Only touched from process_queue_loop_'s one thread.
  //

  fbl::unique_fd sysmem_dir_fd_;
  fidl::ClientEnd<fuchsia_sysmem::DriverConnector> driver_connector_client_;
  async::WaitMethod<SysmemConnector, &SysmemConnector::OnSysmemPeerClosed> wait_sysmem_peer_closed_;

  //
  // Synchronized using lock_.
  //

  fbl::Mutex lock_;
  std::queue<QueueItem> connection_requests_ __TA_GUARDED(lock_);
};

SysmemConnector::SysmemConnector(const char* sysmem_directory_path,
                                 bool terminate_on_sysmem_connection_failure)
    : sysmem_directory_path_(sysmem_directory_path),
      process_queue_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      terminate_on_sysmem_connection_failure_(terminate_on_sysmem_connection_failure),
      wait_sysmem_peer_closed_(this) {
  ZX_DEBUG_ASSERT(sysmem_directory_path_);
}

zx_status_t SysmemConnector::Start() {
  // The process_queue_thrd_ is filled out before any code that checks it runs on the thread,
  // because the current thread is the only thread that triggers anything to happen on the thread
  // being created here, and that is only done after process_queue_thrd_ is filled out by the
  // current thread.
  const zx_status_t status =
      process_queue_loop_.StartThread("SysmemConnector-ProcessQueue", &process_queue_thrd_);
  if (status != ZX_OK) {
    printf("sysmem-connector: process_queue_loop_.StartThread() failed - status: %d\n", status);
    return status;
  }
  // Establish initial connection to sysmem driver async.
  Post([this] { ConnectToSysmemDriver(); });
  return ZX_OK;
}

void SysmemConnector::Queue(QueueItem&& queue_item) {
  ZX_DEBUG_ASSERT(thrd_current() != process_queue_thrd_);
  bool trigger_needed;
  {  // scope lock
    const fbl::AutoLock lock(&lock_);
    trigger_needed = connection_requests_.empty();
    connection_requests_.emplace(std::move(queue_item));
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

void SysmemConnector::Post(fit::closure to_run) {
  const zx_status_t status = async::PostTask(process_queue_loop_.dispatcher(), std::move(to_run));
  // We don't expect this post to ever fail.
  ZX_ASSERT_MSG(status == ZX_OK, "%s", zx_status_get_string(status));
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

  zx::status endpoints = fidl::CreateEndpoints<fuchsia_sysmem::DriverConnector>();
  if (endpoints.is_error()) {
    printf("SysmemConnector::DeviceAdded() zx::channel::create() failed - status: %s\n",
           endpoints.status_string());
    // If channel create fails, give up on this attempt to find a sysmem
    // device instance.  If another request arrives later, we'll try again
    // later.
    return endpoints.status_value();
  }
  auto& [client, server] = endpoints.value();

  const fdio_cpp::UnownedFdioCaller caller(dirfd);
  zx_status_t status;
  status =
      fdio_service_connect_at(caller.borrow_channel(), filename, server.TakeChannel().release());
  if (status != ZX_OK) {
    printf("SysmemConnector::DeviceAdded() fdio_service_connect_at() failed - status: %d\n",
           status);
    ZX_DEBUG_ASSERT(status != ZX_ERR_STOP);
    // If somehow fdio_service_connect_at() fails for this device instance,
    // keep watching for another device instance.
    return ZX_OK;
  }

  if (terminate_on_sysmem_connection_failure_) {
    wait_sysmem_peer_closed_.set_trigger(ZX_CHANNEL_PEER_CLOSED);
    wait_sysmem_peer_closed_.set_object(client.channel().get());
    status = wait_sysmem_peer_closed_.Begin(process_queue_loop_.dispatcher());
    if (status != ZX_OK) {
      ZX_PANIC("Failed begin wait for sysmem failure - terminating process to trigger reboot.\n");
    }
    // Cancel() doesn't need to be called anywhere because this process will terminate immediately
    // if the wait ever completes.
  }

  char process_name[ZX_MAX_NAME_LEN] = "";
  status = zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  ZX_DEBUG_ASSERT(status == ZX_OK);
  printf("sysmem-connector: %s connected to sysmem driver %s\n", process_name, filename);
  fflush(stdout);

  driver_connector_client_ = std::move(client);
  ZX_DEBUG_ASSERT(driver_connector_client_);
  return ZX_ERR_STOP;
}

zx_status_t SysmemConnector::ConnectToSysmemDriver() {
  ZX_DEBUG_ASSERT(thrd_current() == process_queue_thrd_);
  ZX_DEBUG_ASSERT(!driver_connector_client_);

  ZX_DEBUG_ASSERT(!sysmem_dir_fd_);
  {
    const int fd = open(sysmem_directory_path_, O_DIRECTORY | O_RDONLY);
    if (fd < 0) {
      printf("sysmem-connector: Failed to open %s: %d\n", sysmem_directory_path_, errno);
      if (terminate_on_sysmem_connection_failure_) {
        ZX_PANIC("Unable to connect to sysmem - terminating process to trigger reboot.\n");
      }
      return ZX_ERR_INTERNAL;
    }
    sysmem_dir_fd_.reset(fd);
  }
  ZX_DEBUG_ASSERT(sysmem_dir_fd_);

  // Returns ZX_ERR_STOP as soon as one of the 000, 001 device instances is
  // found.  We rely on those to go away if the corresponding sysmem instance
  // is no longer operational, so that we don't find them when we call
  // ConnectToSysmemDriver() again upon discovering that we can't send to a
  // previous device instance.  When terminate_on_sysmem_connection_failure_,
  // there won't be any instances after 000 fails because sysmem_connector will
  // terminate and sysmem_connector is a critical process.
  //
  // TODO(dustingreen): Currently if this watch never finds a sysmem device
  // instance, then sysmem_connector_release() will block forever.  This can
  // be fixed once it's feasible to use DeviceWatcher (or similar) here
  // instead (currently DeviceWatcher is in garnet not zircon).
  const zx_status_t watch_status = fdio_watch_directory(
      sysmem_dir_fd_.get(),
      [](int dirfd, int event, const char* fn, void* cookie) {
        ZX_DEBUG_ASSERT(cookie);
        SysmemConnector* connector = static_cast<SysmemConnector*>(cookie);
        return connector->DeviceAdded(dirfd, event, fn);
      },
      ZX_TIME_INFINITE, this);
  if (watch_status != ZX_ERR_STOP) {
    printf("sysmem-connector: Failed to find sysmem device - status: %d\n", watch_status);
    if (terminate_on_sysmem_connection_failure_) {
      ZX_PANIC("Sysmem device instance not found - terminating process to trigger reboot.\n");
    }
    return watch_status;
  }
  ZX_DEBUG_ASSERT(driver_connector_client_);
  return ZX_OK;
}

namespace {

// Helpers from the reference documentation for std::visit<>, to allow
// visit-by-overload of the std::variant<> returned by GetLastReference():
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
// explicit deduction guide (not needed as of C++20)
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

}  // namespace

void SysmemConnector::ProcessQueue() {
  ZX_DEBUG_ASSERT(thrd_current() == process_queue_thrd_);
  while (true) {
    QueueItem queue_item;
    {  // scope lock
      const fbl::AutoLock lock(&lock_);
      if (connection_requests_.empty()) {
        return;
      }
      queue_item = std::move(connection_requests_.front());
      connection_requests_.pop();
    }  // ~lock

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
      const zx_status_t wait_status =
          zx_object_wait_one(driver_connector_client_.channel().get(), ZX_CHANNEL_PEER_CLOSED,
                             ZX_TIME_INFINITE_PAST, &observed);
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
      const zx_status_t connect_status = ConnectToSysmemDriver();
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

    std::visit(
        overloaded{
            [this](fidl::ServerEnd<fuchsia_sysmem::Allocator> allocator_request) {
              const fidl::WireResult result =
                  fidl::WireCall(driver_connector_client_)->Connect(std::move(allocator_request));
              if (!result.ok()) {
                // The most likely failing status is ZX_ERR_PEER_CLOSED, which
                // can happen if the channel closed since we checked above.
                // Since we don't really expect even ZX_ERR_PEER_CLOSED unless
                // sysmem is having problems, complain about the error
                // regardless of which error.
                printf(
                    "SysmemConnector::ProcessQueue() DriverConnectorConnect() returned unexpected "
                    "status: %s\n",
                    result.status_string());

                // Regardless of the specific error, we want to try
                // ConnectToSysmemDriver() again for the _next_ request.
                driver_connector_client_.reset();

                // We don't retry this request (the window for getting
                // ZX_ERR_PEER_CLOSED is short due to check above, and exists in
                // any case due to possibility of close from other end at any
                // time), but the next request will try ConnectToSysmemDriver()
                // again.
                //
                // continue with next request
              }
            },
            [this](fidl::ClientEnd<fuchsia_io::Directory> service_directory) {
              printf("queue_item.service_directory\n");
              const fidl::WireResult result =
                  fidl::WireCall(driver_connector_client_)
                      ->SetAuxServiceDirectory(std::move(service_directory));
              if (!result.ok()) {
                printf(
                    "SysmemConnector::ProcessQueue() DriverConnectorSetAuxServiceDirectory() "
                    "returned unexpected status: %s\n",
                    result.status_string());
                driver_connector_client_.reset();
                // continue with next request
              }
            }},
        std::move(queue_item));
  }
}

void SysmemConnector::OnSysmemPeerClosed(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                         zx_status_t status, const zx_packet_signal_t* signal) {
  // Else we wouldn't have started the wait that is now completing.
  ZX_ASSERT(terminate_on_sysmem_connection_failure_);
  // Any other wait status is unexpected, so terminate this process.
  ZX_ASSERT(status == ZX_OK);
  // This signal is set because we only waited on this signal.
  ZX_ASSERT((signal->observed & ZX_CHANNEL_PEER_CLOSED) != 0);
  // Terminate sysmem_connector, which is a critical process, so this will do a hard reboot.
  ZX_PANIC(
      "sysmem_connector's connection to sysmem has closed; sysmem driver failed - "
      "terminating process to trigger reboot.\n");
}

zx_status_t sysmem_connector_init(const char* sysmem_directory_path,
                                  bool terminate_on_sysmem_connection_failure,
                                  sysmem_connector_t** out_connector) {
  SysmemConnector* connector =
      new SysmemConnector(sysmem_directory_path, terminate_on_sysmem_connection_failure);
  const zx_status_t status = connector->Start();
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
  connector->Queue(fidl::ServerEnd<fuchsia_sysmem::Allocator>{std::move(allocator_request)});
}

void sysmem_connector_queue_service_directory(sysmem_connector_t* connector_param,
                                              zx_handle_t service_directory_param) {
  printf("sysmem_connector_queue_service_directory\n");
  zx::channel service_directory(service_directory_param);
  ZX_DEBUG_ASSERT(connector_param);
  ZX_DEBUG_ASSERT(service_directory);
  SysmemConnector* connector = static_cast<SysmemConnector*>(connector_param);
  connector->Queue(fidl::ClientEnd<fuchsia_io::Directory>{std::move(service_directory)});
}

void sysmem_connector_release(sysmem_connector_t* connector_param) {
  ZX_DEBUG_ASSERT(connector_param);
  SysmemConnector* connector = static_cast<SysmemConnector*>(connector_param);
  connector->Stop();
  delete connector;
}
