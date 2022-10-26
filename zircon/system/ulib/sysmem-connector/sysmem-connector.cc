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
#include <lib/fdio/watcher.h>
#include <lib/fit/function.h>
#include <lib/sys/component/cpp/service_client.h>
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

  bool ConnectToSysmemDriver();

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
    printf("sysmem-connector: process_queue_loop_.StartThread(): %s\n",
           zx_status_get_string(status));
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
  if (std::string_view{filename} == ".") {
    return ZX_OK;
  }
  if (event != WATCH_EVENT_ADD_FILE) {
    // Keep going on IDLE or REMOVE.  There's nothing else useful that the
    // current thread can do until a sysmem device instance is available,
    // and we don't have any reason to attempt to directly handle any
    // REMOVE(s) since we'll do fdio_watch_directory() again later from
    // scratch instead.
    return ZX_OK;
  }

  {
    const fdio_cpp::UnownedFdioCaller caller(dirfd);
    zx::result status = component::ConnectAt<fuchsia_sysmem::DriverConnector>(
        caller.borrow_as<fuchsia_io::Directory>(), filename);
    if (status.is_error()) {
      printf("sysmem-connector: component::ConnectAt(%s, %s): %s\n", sysmem_directory_path_,
             filename, status.status_string());
      // If somehow connecting to this device instance fails, keep watching for
      // another device instance.
      return ZX_OK;
    }
    driver_connector_client_ = std::move(status.value());
  }

  if (terminate_on_sysmem_connection_failure_) {
    wait_sysmem_peer_closed_.set_trigger(ZX_CHANNEL_PEER_CLOSED);
    wait_sysmem_peer_closed_.set_object(driver_connector_client_.channel().get());
    const zx_status_t status = wait_sysmem_peer_closed_.Begin(process_queue_loop_.dispatcher());
    ZX_ASSERT_MSG(status == ZX_OK, "async::WaitMethod<OnSysmemPeerClosed>::Begin: %s",
                  zx_status_get_string(status));
    // Cancel() doesn't need to be called anywhere because this process will
    // terminate immediately if the wait ever completes.
  }

  char process_name[ZX_MAX_NAME_LEN];
  const zx_status_t status =
      zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  ZX_ASSERT_MSG(status == ZX_OK, "%s", zx_status_get_string(status));
  printf("sysmem-connector: %s connected to sysmem driver %s\n", process_name, filename);

  return ZX_ERR_STOP;
}

bool SysmemConnector::ConnectToSysmemDriver() {
  ZX_DEBUG_ASSERT(thrd_current() == process_queue_thrd_);
  ZX_DEBUG_ASSERT(!driver_connector_client_);

  fbl::unique_fd sysmem_dir_fd;
  {
    const int fd = open(sysmem_directory_path_, O_DIRECTORY | O_RDONLY);
    if (fd < 0) {
      if (terminate_on_sysmem_connection_failure_) {
        ZX_PANIC("open(%s): %s", sysmem_directory_path_, strerror(errno));
      } else {
        printf("sysmem-connector: open(%s): %s\n", sysmem_directory_path_, strerror(errno));
      }
      return false;
    }
    sysmem_dir_fd.reset(fd);
  }
  ZX_DEBUG_ASSERT(sysmem_dir_fd);

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
  const zx_status_t status = fdio_watch_directory(
      sysmem_dir_fd.get(),
      [](int dirfd, int event, const char* fn, void* cookie) {
        ZX_DEBUG_ASSERT(cookie);
        SysmemConnector* connector = static_cast<SysmemConnector*>(cookie);
        return connector->DeviceAdded(dirfd, event, fn);
      },
      ZX_TIME_INFINITE, this);
  if (status != ZX_ERR_STOP) {
    if (terminate_on_sysmem_connection_failure_) {
      ZX_PANIC("fdio_watch_directory(%s): %s", sysmem_directory_path_,
               zx_status_get_string(status));
    } else {
      printf("sysmem-connector: fdio_watch_directory(%s): %s\n", sysmem_directory_path_,
             zx_status_get_string(status));
    }
    return false;
  }
  ZX_DEBUG_ASSERT(driver_connector_client_);
  return true;
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

    if (!driver_connector_client_) {
      if (!ConnectToSysmemDriver()) {
        // ~queue_item - we'll try again to connect to a sysmem instance next
        // time a request comes in, but any given request gets a max of one
        // attempt to connect to a sysmem device instance, in case attempts to
        // find a sysmem device instance are just failing.
        return;
      }
    }
    ZX_DEBUG_ASSERT(driver_connector_client_);

    const auto [name, status] = std::visit(
        overloaded{[this](fidl::ServerEnd<fuchsia_sysmem::Allocator> allocator_request) {
                     return std::make_pair("Connect", fidl::WireCall(driver_connector_client_)
                                                          ->Connect(std::move(allocator_request))
                                                          .status());
                   },
                   [this](fidl::ClientEnd<fuchsia_io::Directory> service_directory) {
                     return std::make_pair(
                         "SetAuxServiceDirectory",
                         fidl::WireCall(driver_connector_client_)
                             ->SetAuxServiceDirectory(std::move(service_directory))
                             .status());
                   }},
        std::move(queue_item));
    printf("sysmem-connector: fuchsia.sysmem/DriverConnect.%s: %s", name,
           zx_status_get_string(status));
    if (status != ZX_OK) {
      driver_connector_client_.reset();
    }
  }
}

void SysmemConnector::OnSysmemPeerClosed(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                         zx_status_t status, const zx_packet_signal_t* signal) {
  // Else we wouldn't have started the wait that is now completing.
  ZX_ASSERT(terminate_on_sysmem_connection_failure_);
  // Any other wait status is unexpected, so terminate this process.
  ZX_ASSERT_MSG(status == ZX_OK, "%s", zx_status_get_string(status));
  // This signal is set because we only waited on this signal.
  ZX_ASSERT_MSG((signal->observed & ZX_CHANNEL_PEER_CLOSED) != 0, "0x%x", signal->observed);
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
    printf("sysmem_connector_init() connector->Start() failed - status: %s\n",
           zx_status_get_string(status));
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
