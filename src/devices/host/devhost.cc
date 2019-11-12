// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devhost.h"

#include <dlfcn.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/device/manager/llcpp/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/receiver.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/coding.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <memory>
#include <new>
#include <utility>
#include <vector>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>

#include "async-loop-owned-rpc-handler.h"
#include "composite-device.h"
#include "connection-destroyer.h"
#include "device-controller-connection.h"
#include "env.h"
#include "fidl_txn.h"
#include "log.h"
#include "main.h"
#include "proxy-iostate.h"
#include "scheduler_profile.h"
#include "tracing.h"

zx_status_t zx_driver::Create(fbl::RefPtr<zx_driver>* out_driver) {
  *out_driver = fbl::AdoptRef(new zx_driver());
  return ZX_OK;
}

namespace devmgr {

uint32_t log_flags = LOG_ERROR | LOG_INFO;

static fbl::DoublyLinkedList<fbl::RefPtr<zx_driver>> dh_drivers;

DevhostContext& DevhostCtx() {
  static DevhostContext ctx(&kAsyncLoopConfigAttachToCurrentThread);
  return ctx;
}

// Access the devhost's async event loop
async::Loop* DevhostAsyncLoop() { return &DevhostCtx().loop(); }

static zx_status_t SetupRootDevcoordinatorConnection(zx::channel ch) {
  auto conn = std::make_unique<DevhostControllerConnection>();
  if (conn == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  conn->set_channel(std::move(ch));
  return DevhostControllerConnection::BeginWait(std::move(conn), DevhostAsyncLoop()->dispatcher());
}

const char* mkdevpath(const fbl::RefPtr<zx_device_t>& dev, char* path, size_t max) {
  if (dev == nullptr) {
    return "";
  }
  if (max < 1) {
    return "<invalid>";
  }
  char* end = path + max;
  char sep = 0;

  fbl::RefPtr<zx_device> itr_dev(dev);
  while (itr_dev) {
    *(--end) = sep;

    size_t len = strlen(itr_dev->name);
    if (len > (size_t)(end - path)) {
      break;
    }
    end -= len;
    memcpy(end, itr_dev->name, len);
    sep = '/';
    itr_dev = itr_dev->parent;
  }
  return end;
}

static uint32_t logflagval(char* flag) {
  if (!strcmp(flag, "error")) {
    return DDK_LOG_ERROR;
  }
  if (!strcmp(flag, "warn")) {
    return DDK_LOG_WARN;
  }
  if (!strcmp(flag, "info")) {
    return DDK_LOG_INFO;
  }
  if (!strcmp(flag, "trace")) {
    return DDK_LOG_TRACE;
  }
  if (!strcmp(flag, "spew")) {
    return DDK_LOG_SPEW;
  }
  if (!strcmp(flag, "debug1")) {
    return DDK_LOG_DEBUG1;
  }
  if (!strcmp(flag, "debug2")) {
    return DDK_LOG_DEBUG2;
  }
  if (!strcmp(flag, "debug3")) {
    return DDK_LOG_DEBUG3;
  }
  if (!strcmp(flag, "debug4")) {
    return DDK_LOG_DEBUG4;
  }
  return static_cast<uint32_t>(strtoul(flag, nullptr, 0));
}

static void logflag(char* flag, uint32_t* flags) {
  if (*flag == '+') {
    *flags |= logflagval(flag + 1);
  } else if (*flag == '-') {
    *flags &= ~logflagval(flag + 1);
  }
}

zx_status_t dh_find_driver(fbl::StringPiece libname, zx::vmo vmo, fbl::RefPtr<zx_driver_t>* out) {
  // check for already-loaded driver first
  for (auto& drv : dh_drivers) {
    if (!libname.compare(drv.libname())) {
      *out = fbl::RefPtr(&drv);
      return drv.status();
    }
  }

  fbl::RefPtr<zx_driver> new_driver;
  zx_status_t status = zx_driver::Create(&new_driver);
  if (status != ZX_OK) {
    return status;
  }
  new_driver->set_libname(libname);

  // Let the |dh_drivers| list and our out parameter each have a refcount.
  dh_drivers.push_back(new_driver);
  *out = new_driver;

  const char* c_libname = new_driver->libname().c_str();

  void* dl = dlopen_vmo(vmo.get(), RTLD_NOW);
  if (dl == nullptr) {
    log(ERROR, "devhost: cannot load '%s': %s\n", c_libname, dlerror());
    new_driver->set_status(ZX_ERR_IO);
    return new_driver->status();
  }

  auto dn = static_cast<const zircon_driver_note_t*>(dlsym(dl, "__zircon_driver_note__"));
  if (dn == nullptr) {
    log(ERROR, "devhost: driver '%s' missing __zircon_driver_note__ symbol\n", c_libname);
    new_driver->set_status(ZX_ERR_IO);
    return new_driver->status();
  }
  auto ops = static_cast<const zx_driver_ops_t**>(dlsym(dl, "__zircon_driver_ops__"));
  auto dr = static_cast<zx_driver_rec_t*>(dlsym(dl, "__zircon_driver_rec__"));
  if (dr == nullptr) {
    log(ERROR, "devhost: driver '%s' missing __zircon_driver_rec__ symbol\n", c_libname);
    new_driver->set_status(ZX_ERR_IO);
    return new_driver->status();
  }
  // TODO(kulakowski) Eventually just check __zircon_driver_ops__,
  // when bind programs are standalone.
  if (ops == nullptr) {
    ops = &dr->ops;
  }
  if (!(*ops)) {
    log(ERROR, "devhost: driver '%s' has nullptr ops\n", c_libname);
    new_driver->set_status(ZX_ERR_INVALID_ARGS);
    return new_driver->status();
  }
  if ((*ops)->version != DRIVER_OPS_VERSION) {
    log(ERROR,
        "devhost: driver '%s' has bad driver ops version %" PRIx64 ", expecting %" PRIx64 "\n",
        c_libname, (*ops)->version, DRIVER_OPS_VERSION);
    new_driver->set_status(ZX_ERR_INVALID_ARGS);
    return new_driver->status();
  }

  new_driver->set_driver_rec(dr);
  new_driver->set_name(dn->payload.name);
  new_driver->set_ops(*ops);
  dr->driver = new_driver.get();

  // check for dprintf log level flags
  char tmp[128];
  snprintf(tmp, sizeof(tmp), "driver.%s.log", new_driver->name());
  char* log = getenv(tmp);
  if (log) {
    while (log) {
      char* sep = strchr(log, ',');
      if (sep) {
        *sep = 0;
        logflag(log, &dr->log_flags);
        *sep = ',';
        log = sep + 1;
      } else {
        logflag(log, &dr->log_flags);
        break;
      }
    }
    log(INFO, "devhost: driver '%s': log flags set to: 0x%x\n", new_driver->name(), dr->log_flags);
  }

  if (new_driver->has_init_op()) {
    new_driver->set_status(new_driver->InitOp());
    if (new_driver->status() != ZX_OK) {
      log(ERROR, "devhost: driver '%s' failed in init: %d\n", c_libname, new_driver->status());
    }
  } else {
    new_driver->set_status(ZX_OK);
  }

  return new_driver->status();
}

void DevhostControllerConnection::CreateDevice(zx::channel rpc, ::fidl::StringView driver_path_view,
                                               ::zx::vmo driver_vmo, ::zx::handle parent_proxy,
                                               ::fidl::StringView proxy_args,
                                               uint64_t local_device_id,
                                               CreateDeviceCompleter::Sync completer) {
  fbl::StringPiece driver_path(driver_path_view.data(), driver_path_view.size());
  // This does not operate under the devhost api lock,
  // since the newly created device is not visible to
  // any API surface until a driver is bound to it.
  // (which can only happen via another message on this thread)
  log(RPC_IN, "devhost: create device drv='%.*s' args='%.*s'\n",
      static_cast<int>(driver_path.size()), driver_path.data(), static_cast<int>(proxy_args.size()),
      proxy_args.data());

  // named driver -- ask it to create the device
  fbl::RefPtr<zx_driver_t> drv;
  zx_status_t r = dh_find_driver(driver_path, std::move(driver_vmo), &drv);
  if (r != ZX_OK) {
    log(ERROR, "devhost: driver load failed: %d\n", r);
    return;
  }
  if (!drv->has_create_op()) {
    log(ERROR, "devhost: driver create() not supported\n");
    return;
  }

  // Create a dummy parent device for use in this call to Create
  fbl::RefPtr<zx_device> parent;
  if ((r = zx_device::Create(&parent)) != ZX_OK) {
    log(ERROR, "devhost: device create() failed: %d\n", r);
    return;
  }
  // magic cookie for device create handshake
  char dummy_name[sizeof(parent->name)] = "device_create dummy";
  memcpy(&parent->name, &dummy_name, sizeof(parent->name));

  CreationContext creation_context = {
      .parent = std::move(parent),
      .child = nullptr,
      .rpc = zx::unowned_channel(rpc),
  };

  r = drv->CreateOp(&creation_context, creation_context.parent, "proxy", proxy_args.data(),
                    parent_proxy.release());

  // Suppress a warning about dummy device being in a bad state.  The
  // message is spurious in this case, since the dummy parent never
  // actually begins its device lifecycle.  This flag is ordinarily
  // set by device_remove().
  creation_context.parent->flags |= DEV_FLAG_DEAD;

  if (r != ZX_OK) {
    log(ERROR, "devhost: driver create() failed: %d\n", r);
    return;
  }

  auto new_device = std::move(creation_context.child);
  if (new_device == nullptr) {
    log(ERROR, "devhost: driver create() failed to create a device!");
    return;
  }

  new_device->set_local_id(local_device_id);
  std::unique_ptr<DeviceControllerConnection> newconn;
  r = DeviceControllerConnection::Create(std::move(new_device), std::move(rpc), &newconn);
  if (r != ZX_OK) {
    return;
  }

  // TODO: inform devcoord

  log(RPC_IN, "devhost: creating '%.*s' conn=%p\n", static_cast<int>(driver_path.size()),
      driver_path.data(), newconn.get());
  if ((r = DeviceControllerConnection::BeginWait(std::move(newconn),
                                                 DevhostAsyncLoop()->dispatcher())) != ZX_OK) {
    return;
  }
}

void DevhostControllerConnection::CreateCompositeDevice(
    zx::channel rpc, ::fidl::VectorView<uint64_t> components, ::fidl::StringView name,
    uint64_t local_device_id, CreateCompositeDeviceCompleter::Sync completer) {
  log(RPC_IN, "devhost: create composite device %.*s'\n", static_cast<int>(name.size()),
      name.data());

  // Convert the component IDs into zx_device references
  CompositeComponents components_list(new fbl::RefPtr<zx_device>[components.count()],
                                      components.count());
  {
    // Acquire the API lock so that we don't have to worry about concurrent
    // device removes
    ApiAutoLock lock;

    for (size_t i = 0; i < components.count(); ++i) {
      uint64_t local_id = components.data()[i];
      fbl::RefPtr<zx_device_t> dev = zx_device::GetDeviceFromLocalId(local_id);
      if (dev == nullptr || (dev->flags & DEV_FLAG_DEAD)) {
        completer.Reply(ZX_ERR_NOT_FOUND);
        return;
      }
      components_list[i] = std::move(dev);
    }
  }

  fbl::RefPtr<zx_device_t> dev;
  zx_status_t status = zx_device::Create(&dev);
  if (status != ZX_OK) {
    completer.Reply(status);
    return;
  }
  static_assert(fuchsia_device_manager_DEVICE_NAME_MAX + 1 >= sizeof(dev->name));
  memcpy(dev->name, name.data(), name.size());
  dev->name[name.size()] = 0;
  dev->set_local_id(local_device_id);

  std::unique_ptr<DeviceControllerConnection> newconn;
  status = DeviceControllerConnection::Create(dev, std::move(rpc), &newconn);
  if (status != ZX_OK) {
    completer.Reply(status);
    return;
  }

  status = InitializeCompositeDevice(dev, std::move(components_list));
  if (status != ZX_OK) {
    completer.Reply(status);
    return;
  }

  log(RPC_IN, "devhost: creating new composite conn=%p\n", newconn.get());
  if ((status = DeviceControllerConnection::BeginWait(std::move(newconn),
                                                      DevhostAsyncLoop()->dispatcher())) != ZX_OK) {
    completer.Reply(status);
    return;
  }
  completer.Reply(ZX_OK);
}

void DevhostControllerConnection::CreateDeviceStub(zx::channel rpc, uint32_t protocol_id,
                                                   uint64_t local_device_id,
                                                   CreateDeviceStubCompleter::Sync completer) {
  log(RPC_IN, "devhost: create device stub\n");

  fbl::RefPtr<zx_device_t> dev;
  zx_status_t r = zx_device::Create(&dev);
  // TODO: dev->ops and other lifecycle bits
  // no name means a dummy proxy device
  if (r != ZX_OK) {
    return;
  }
  strcpy(dev->name, "proxy");
  dev->protocol_id = protocol_id;
  dev->ops = &device_default_ops;
  dev->set_local_id(local_device_id);

  std::unique_ptr<DeviceControllerConnection> newconn;
  r = DeviceControllerConnection::Create(dev, std::move(rpc), &newconn);
  if (r != ZX_OK) {
    return;
  }

  log(RPC_IN, "devhost: creating new stub conn=%p\n", newconn.get());
  if ((r = DeviceControllerConnection::BeginWait(std::move(newconn),
                                                 DevhostAsyncLoop()->dispatcher())) != ZX_OK) {
    return;
  }
}

zx_status_t DevhostControllerConnection::HandleRead() {
  zx::unowned_channel conn = channel();
  uint8_t msg[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t hin[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t msize = sizeof(msg);
  uint32_t hcount = fbl::count_of(hin);
  zx_status_t status = conn->read(0, msg, hin, msize, hcount, &msize, &hcount);
  if (status != ZX_OK) {
    return status;
  }

  fidl_msg_t fidl_msg = {
      .bytes = msg,
      .handles = hin,
      .num_bytes = msize,
      .num_handles = hcount,
  };

  if (fidl_msg.num_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(fidl_msg.handles, fidl_msg.num_handles);
    return ZX_ERR_IO;
  }

  auto hdr = static_cast<fidl_message_header_t*>(fidl_msg.bytes);
  DevmgrFidlTxn txn(std::move(conn), hdr->txid);
  fuchsia::device::manager::DevhostController::Dispatch(this, &fidl_msg, &txn);
  return txn.Status();
}

// handles devcoordinator rpc

void DevhostControllerConnection::HandleRpc(std::unique_ptr<DevhostControllerConnection> conn,
                                            async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                            zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    log(ERROR, "devhost: devcoord conn wait error: %d\n", status);
    return;
  }
  if (signal->observed & ZX_CHANNEL_READABLE) {
    status = conn->HandleRead();
    if (status != ZX_OK) {
      log(ERROR, "devhost: devmgr rpc unhandleable ios=%p r=%s. fatal.\n", conn.get(),
          zx_status_get_string(status));
      abort();
    }
    BeginWait(std::move(conn), dispatcher);
    return;
  }
  if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
    log(ERROR, "devhost: devmgr disconnected! fatal. (conn=%p)\n", conn.get());
    abort();
  }
  log(ERROR, "devhost: no work? %08x\n", signal->observed);
  BeginWait(std::move(conn), dispatcher);
}

void proxy_ios_destroy(const fbl::RefPtr<zx_device_t>& dev) {
  fbl::AutoLock guard(&dev->proxy_ios_lock);

  if (dev->proxy_ios) {
    dev->proxy_ios->CancelLocked(DevhostAsyncLoop()->dispatcher());
  }
}

static zxio_t* devhost_zxio_logger = nullptr;

}  // namespace devmgr

__EXPORT void driver_printf(uint32_t flags, const char* fmt, ...) {
  if (devmgr::devhost_zxio_logger == nullptr) {
    return;
  }
  char buffer[512];
  va_list ap;
  va_start(ap, fmt);
  size_t r = vsnprintf(buffer, sizeof(buffer), fmt, ap);
  va_end(ap);

  size_t actual;
  zxio_write(devmgr::devhost_zxio_logger, buffer, std::min(r, sizeof(buffer)), 0, &actual);
}

namespace devmgr {

zx_handle_t root_resource_handle;

static void devhost_io_init() {
  zx::debuglog handle;
  if (zx::debuglog::create(*zx::unowned_resource(root_resource_handle), 0, &handle) != ZX_OK) {
    return;
  }
  zxio_storage_t* storage;
  fdio_t* io = fdio_zxio_create(&storage);
  if (io == nullptr) {
    return;
  }
  if (zxio_debuglog_init(storage, std::move(handle)) != ZX_OK) {
    return;
  }
  devhost_zxio_logger = &storage->io;
  close(1);
  fdio_bind_to_fd(io, 1, 0);
  dup2(1, 2);
}

// Send message to devcoordinator asking to add child device to
// parent device.  Called under devhost api lock.
zx_status_t devhost_add(const fbl::RefPtr<zx_device_t>& parent,
                        const fbl::RefPtr<zx_device_t>& child, const char* proxy_args,
                        const zx_device_prop_t* props, uint32_t prop_count,
                        zx::channel client_remote) {
  char buffer[512];
  const char* path = mkdevpath(parent, buffer, sizeof(buffer));
  log(RPC_OUT, "devhost[%s] add '%s'\n", path, child->name);

  bool add_invisible = child->flags & DEV_FLAG_INVISIBLE;
  fuchsia::device::manager::AddDeviceConfig add_device_config;

  if (child->flags & DEV_FLAG_ALLOW_MULTI_COMPOSITE) {
    add_device_config |= fuchsia::device::manager::AddDeviceConfig::ALLOW_MULTI_COMPOSITE;
  }

  zx_status_t status;
  zx::channel hrpc, hsend;
  if ((status = zx::channel::create(0, &hrpc, &hsend)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<DeviceControllerConnection> conn;
  status = DeviceControllerConnection::Create(child, std::move(hrpc), &conn);
  if (status != ZX_OK) {
    return status;
  }

  const zx::channel& rpc = *parent->rpc;
  if (!rpc.is_valid()) {
    return ZX_ERR_IO_REFUSED;
  }
  size_t proxy_args_len = proxy_args ? strlen(proxy_args) : 0;
  zx_status_t call_status = ZX_OK;
  static_assert(sizeof(zx_device_prop_t) == sizeof(uint64_t));
  uint64_t device_id = 0;
  if (add_invisible) {
    auto response = fuchsia::device::manager::Coordinator::Call::AddDeviceInvisible(
        zx::unowned_channel(rpc.get()), std::move(hsend),
        ::fidl::VectorView(reinterpret_cast<uint64_t*>(const_cast<zx_device_prop_t*>(props)),
                           prop_count),
        ::fidl::StringView(child->name, strlen(child->name)), child->protocol_id,
        ::fidl::StringView(child->driver->libname()),
        ::fidl::StringView(proxy_args, proxy_args_len), std::move(client_remote));
    status = response.status();
    if (status == ZX_OK) {
      if (response.Unwrap()->result.is_response()) {
        device_id = response.Unwrap()->result.response().local_device_id;
      } else {
        call_status = response.Unwrap()->result.err();
      }
    }
  } else {
    auto response = fuchsia::device::manager::Coordinator::Call::AddDevice(
        zx::unowned_channel(rpc.get()), std::move(hsend),
        ::fidl::VectorView(reinterpret_cast<uint64_t*>(const_cast<zx_device_prop_t*>(props)),
                           prop_count),
        ::fidl::StringView(child->name, strlen(child->name)), child->protocol_id,
        ::fidl::StringView(child->driver->libname()),
        ::fidl::StringView(proxy_args, proxy_args_len), add_device_config,
        std::move(client_remote));
    status = response.status();
    if (status == ZX_OK) {
      if (response.Unwrap()->result.is_response()) {
        device_id = response.Unwrap()->result.response().local_device_id;
      } else {
        call_status = response.Unwrap()->result.err();
      }
    }
  }
  if (status != ZX_OK) {
    log(ERROR, "devhost[%s] add '%s': rpc sending failed: %d\n", path, child->name, status);
    return status;
  } else if (call_status != ZX_OK) {
    log(ERROR, "devhost[%s] add '%s': rpc failed: %d\n", path, child->name, call_status);
    return call_status;
  }

  child->set_local_id(device_id);

  status = DeviceControllerConnection::BeginWait(std::move(conn), DevhostAsyncLoop()->dispatcher());
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

static void log_rpc(const fbl::RefPtr<zx_device_t>& dev, const char* opname) {
  char buffer[512];
  const char* path = mkdevpath(dev, buffer, sizeof(buffer));
  log(RPC_OUT, "devhost[%s] %s'\n", path, opname);
}

static void log_rpc_result(const char* opname, zx_status_t status,
                           zx_status_t call_status = ZX_OK) {
  if (status != ZX_OK) {
    log(ERROR, "devhost: rpc:%s sending failed: %d\n", opname, status);
  } else if (call_status != ZX_OK) {
    log(ERROR, "devhost: rpc:%s failed: %d\n", opname, call_status);
  }
}

void devhost_make_visible(const fbl::RefPtr<zx_device_t>& dev,
                          const device_make_visible_args_t* args) {
  const zx::channel& rpc = *dev->rpc;
  if (!rpc.is_valid()) {
    return;
  }

  if (args && args->power_states && args->power_state_count != 0) {
    dev->SetPowerStates(args->power_states, args->power_state_count);
  }
  if (args && args->performance_states && (args->performance_state_count != 0)) {
    dev->SetPerformanceStates(args->performance_states,
                                        args->performance_state_count);
  }

  // TODO(teisenbe): Handle failures here...
  log_rpc(dev, "make-visible");
  auto response =
      fuchsia::device::manager::Coordinator::Call::MakeVisible(zx::unowned_channel(rpc.get()));
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK) {
    if (response.Unwrap()->result.is_err()) {
      call_status = response.Unwrap()->result.err();
    }
  }
  log_rpc_result("make-visible", status, call_status);
}

// Send message to devcoordinator informing it that this device
// is being removed.  Called under devhost api lock.
zx_status_t devhost_remove(fbl::RefPtr<zx_device_t> dev) {
  DeviceControllerConnection* conn = dev->conn.load();
  if (conn == nullptr) {
    log(ERROR, "removing device %p, conn is nullptr\n", dev.get());
    return ZX_ERR_INTERNAL;
  }

  // This must be done before the RemoveDevice message is sent to
  // devcoordinator, since devcoordinator will close the channel in response.
  // The async loop may see the channel close before it sees the queued
  // shutdown packet, so it needs to check if dev->conn has been nulled to
  // handle that gracefully.
  dev->conn.store(nullptr);

  log(DEVLC, "removing device %p, conn %p\n", dev.get(), conn);

  const zx::channel& rpc = *dev->rpc;
  ZX_ASSERT(rpc.is_valid());
  // TODO(teisenbe): Handle failures here...

  log_rpc(dev, "remove-done");
  auto resp =
      fuchsia::device::manager::Coordinator::Call::RemoveDone(zx::unowned_channel(rpc.get()));
  zx_status_t call_status = ZX_OK;
  if (resp.status() == ZX_OK && resp->result.is_err()) {
    call_status = resp->result.err();
  }
  log_rpc_result("remove-done", resp.status(), call_status);

  // Forget our local ID, to release the reference stored by the local ID map
  dev->set_local_id(0);

  // Forget about our rpc channel since after the port_queue below it may be
  // closed.
  dev->rpc = zx::unowned_channel();

  // queue an event to destroy the connection
  ConnectionDestroyer::Get()->QueueDeviceControllerConnection(DevhostAsyncLoop()->dispatcher(),
                                                              conn);

  // shut down our proxy rpc channel if it exists
  proxy_ios_destroy(dev);

  return ZX_OK;
}

zx_status_t devhost_send_unbind_done(const fbl::RefPtr<zx_device_t>& dev) {
  const zx::channel& rpc = *dev->rpc;
  ZX_ASSERT(rpc.is_valid());
  log_rpc(dev, "unbind-done");
  auto resp =
      fuchsia::device::manager::Coordinator::Call::UnbindDone(zx::unowned_channel(rpc.get()));
  zx_status_t call_status = ZX_OK;
  if (resp.status() == ZX_OK && resp->result.is_err()) {
    call_status = resp->result.err();
  }
  log_rpc_result("unbind-done", resp.status(), call_status);
  return resp.status();
}

zx_status_t devhost_schedule_remove(const fbl::RefPtr<zx_device_t>& dev, bool unbind_self) {
  const zx::channel& rpc = *dev->rpc;
  ZX_ASSERT(rpc.is_valid());
  log_rpc(dev, "schedule-remove");
  auto resp = fuchsia::device::manager::Coordinator::Call::ScheduleRemove(
      zx::unowned_channel(rpc.get()), unbind_self);
  log_rpc_result("schedule-remove", resp.status());
  return resp.status();
}

zx_status_t devhost_schedule_unbind_children(const fbl::RefPtr<zx_device_t>& dev) {
  const zx::channel& rpc = *dev->rpc;
  ZX_ASSERT(rpc.is_valid());
  log_rpc(dev, "schedule-unbind-children");
  auto resp = fuchsia::device::manager::Coordinator::Call::ScheduleUnbindChildren(
      zx::unowned_channel(rpc.get()));
  log_rpc_result("schedule-unbind-children", resp.status());
  return resp.status();
}

zx_status_t devhost_get_topo_path(const fbl::RefPtr<zx_device_t>& dev, char* path, size_t max,
                                  size_t* actual) {
  fbl::RefPtr<zx_device_t> remote_dev = dev;
  if (dev->flags & DEV_FLAG_INSTANCE) {
    // Instances cannot be opened a second time. If dev represents an instance, return the path
    // to its parent, prefixed with an '@'.
    if (max < 1) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    path[0] = '@';
    path++;
    max--;
    remote_dev = dev->parent;
  }

  const zx::channel& rpc = *remote_dev->rpc;
  if (!rpc.is_valid()) {
    return ZX_ERR_IO_REFUSED;
  }

  log_rpc(remote_dev, "get-topo-path");
  auto response = fuchsia::device::manager::Coordinator::Call::GetTopologicalPath(
      zx::unowned_channel(rpc.get()));
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK) {
    if (response.Unwrap()->result.is_err()) {
      call_status = response.Unwrap()->result.err();
    } else {
      auto r = response.Unwrap()->result.response();
      memcpy(path, r.path.data(), r.path.size());
      *actual = r.path.size();
    }
  }

  log_rpc_result("get-topo-path", status, call_status);
  if (status != ZX_OK) {
    return status;
  }
  if (call_status != ZX_OK) {
    return status;
  }

  path[*actual] = 0;
  *actual += 1;

  // Account for the prefixed '@' we may have added above.
  if (dev->flags & DEV_FLAG_INSTANCE) {
    *actual += 1;
  }
  return ZX_OK;
}

zx_status_t devhost_device_bind(const fbl::RefPtr<zx_device_t>& dev, const char* drv_libname) {
  const zx::channel& rpc = *dev->rpc;
  if (!rpc.is_valid()) {
    return ZX_ERR_IO_REFUSED;
  }
  log_rpc(dev, "bind-device");
  auto driver_path = ::fidl::StringView(drv_libname, strlen(drv_libname));
  auto response = fuchsia::device::manager::Coordinator::Call::BindDevice(
      zx::unowned_channel(rpc.get()), driver_path);
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK && response.Unwrap()->result.is_err()) {
    call_status = response.Unwrap()->result.err();
  }
  log_rpc_result("bind-device", status, call_status);
  if (status != ZX_OK) {
    return status;
  }
  return call_status;
}

zx_status_t devhost_device_run_compatibility_tests(const fbl::RefPtr<zx_device_t>& dev,
                                                   int64_t hook_wait_time) {
  const zx::channel& rpc = *dev->rpc;
  if (!rpc.is_valid()) {
    return ZX_ERR_IO_REFUSED;
  }
  log_rpc(dev, "run-compatibility-test");
  auto response = fuchsia::device::manager::Coordinator::Call::RunCompatibilityTests(
      zx::unowned_channel(rpc.get()), hook_wait_time);
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK && response.Unwrap()->result.is_err()) {
    call_status = response.Unwrap()->result.err();
  }
  log_rpc_result("run-compatibility-test", status, call_status);
  if (status != ZX_OK) {
    return status;
  }
  return call_status;
}

zx_status_t devhost_load_firmware(const fbl::RefPtr<zx_device_t>& dev, const char* path,
                                  zx_handle_t* vmo_handle, size_t* size) {
  if ((vmo_handle == nullptr) || (size == nullptr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx::vmo vmo;
  const zx::channel& rpc = *dev->rpc;
  if (!rpc.is_valid()) {
    return ZX_ERR_IO_REFUSED;
  }
  log_rpc(dev, "load-firmware");
  auto str_path = ::fidl::StringView(path, strlen(path));
  auto response = fuchsia::device::manager::Coordinator::Call::LoadFirmware(
      zx::unowned_channel(rpc.get()), str_path);
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  auto result = std::move(response.Unwrap()->result);
  if (result.is_err()) {
    call_status = result.err();
  } else {
    auto resp = std::move(result.mutable_response());
    *size = resp.size;
    vmo = std::move(resp.vmo);
  }
  log_rpc_result("load-firmware", status, call_status);
  if (status != ZX_OK) {
    return status;
  }
  *vmo_handle = vmo.release();
  if (call_status == ZX_OK && *vmo_handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_INTERNAL;
  }
  return call_status;
}

zx_status_t devhost_get_metadata(const fbl::RefPtr<zx_device_t>& dev, uint32_t type, void* buf,
                                 size_t buflen, size_t* actual) {
  if (!buf) {
    return ZX_ERR_INVALID_ARGS;
  }

  const zx::channel& rpc = *dev->rpc;
  if (!rpc.is_valid()) {
    return ZX_ERR_IO_REFUSED;
  }
  log_rpc(dev, "get-metadata");
  auto response = fuchsia::device::manager::Coordinator::Call::GetMetadata(
      zx::unowned_channel(rpc.get()), type);
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK && response.Unwrap()->result.is_err()) {
    call_status = response.Unwrap()->result.err();
  }

  if (status != ZX_OK) {
    log(ERROR, "devhost: rpc:get-metadata sending failed: %d\n", status);
    return status;
  }
  if (call_status != ZX_OK) {
    if (call_status != ZX_ERR_NOT_FOUND) {
      log(ERROR, "devhost: rpc:get-metadata failed: %d\n", call_status);
    }
    return call_status;
  }
  auto r = response.Unwrap()->result.response();
  if (r.data.count() > buflen) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(buf, r.data.data(), r.data.count());
  if (actual != nullptr) {
    *actual = r.data.count();
  }
  return ZX_OK;
}

zx_status_t devhost_get_metadata_size(const fbl::RefPtr<zx_device_t>& dev, uint32_t type,
                                      size_t* out_length) {
  const zx::channel& rpc = *dev->rpc;
  if (!rpc.is_valid()) {
    return ZX_ERR_IO_REFUSED;
  }
  log_rpc(dev, "get-metadata");
  auto response = fuchsia::device::manager::Coordinator::Call::GetMetadataSize(
      zx::unowned_channel(rpc.get()), type);
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK && response.Unwrap()->result.is_err()) {
    call_status = response.Unwrap()->result.err();
  }
  if (status != ZX_OK) {
    log(ERROR, "devhost: rpc:get-metadata sending failed: %d\n", status);
    return status;
  }
  if (call_status != ZX_OK) {
    if (call_status != ZX_ERR_NOT_FOUND) {
      log(ERROR, "devhost: rpc:get-metadata failed: %d\n", call_status);
    }
    return call_status;
  }
  *out_length = response.Unwrap()->result.response().size;
  return ZX_OK;
}

zx_status_t devhost_add_metadata(const fbl::RefPtr<zx_device_t>& dev, uint32_t type,
                                 const void* data, size_t length) {
  if (!data && length) {
    return ZX_ERR_INVALID_ARGS;
  }
  const zx::channel& rpc = *dev->rpc;
  if (!rpc.is_valid()) {
    return ZX_ERR_IO_REFUSED;
  }
  log_rpc(dev, "add-metadata");
  auto response = fuchsia::device::manager::Coordinator::Call::AddMetadata(
      zx::unowned_channel(rpc.get()), type,
      ::fidl::VectorView(reinterpret_cast<uint8_t*>(const_cast<void*>(data)), length));
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK && response.Unwrap()->result.is_err()) {
    call_status = response.Unwrap()->result.err();
  }
  if (status != ZX_OK) {
    return status;
  }
  return call_status;
}

zx_status_t devhost_publish_metadata(const fbl::RefPtr<zx_device_t>& dev, const char* path,
                                     uint32_t type, const void* data, size_t length) {
  if (!path || (!data && length)) {
    return ZX_ERR_INVALID_ARGS;
  }
  const zx::channel& rpc = *dev->rpc;
  if (!rpc.is_valid()) {
    return ZX_ERR_IO_REFUSED;
  }
  log_rpc(dev, "publish-metadata");
  auto response = fuchsia::device::manager::Coordinator::Call::PublishMetadata(
      zx::unowned_channel(rpc.get()), ::fidl::StringView(path, strlen(path)), type,
      ::fidl::VectorView(reinterpret_cast<uint8_t*>(const_cast<void*>(data)), length));
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK && response.Unwrap()->result.is_err()) {
    call_status = response.Unwrap()->result.err();
  }
  log_rpc_result("publish-metadata", status, call_status);
  if (status != ZX_OK) {
    return status;
  }
  return call_status;
}

zx_status_t devhost_device_add_composite(const fbl::RefPtr<zx_device_t>& dev, const char* name,
                                         const composite_device_desc_t* comp_desc) {
  if (comp_desc == nullptr || (comp_desc->props == nullptr && comp_desc->props_count > 0) ||
      comp_desc->components == nullptr || name == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  const zx::channel& rpc = *dev->rpc;
  if (!rpc.is_valid()) {
    return ZX_ERR_IO_REFUSED;
  }

  std::vector<fuchsia::device::manager::DeviceComponent> compvec = {};
  for (size_t i = 0; i < comp_desc->components_count; i++) {
    ::fidl::Array<fuchsia::device::manager::DeviceComponentPart, 16> parts{};
    for (uint32_t j = 0; j < comp_desc->components[i].parts_count; j++) {
      ::fidl::Array<fuchsia::device::manager::BindInstruction, 32> bind_instructions{};
      for (uint32_t k = 0; k < comp_desc->components[i].parts[j].instruction_count; k++) {
        bind_instructions[k] = fuchsia::device::manager::BindInstruction{
            .op = comp_desc->components[i].parts[j].match_program[k].op,
            .arg = comp_desc->components[i].parts[j].match_program[k].arg,
        };
      }
      auto part = fuchsia::device::manager::DeviceComponentPart{
          .match_program_count = comp_desc->components[i].parts[j].instruction_count,
          .match_program = bind_instructions,
      };
      parts[j] = part;
    }
    auto dc = fuchsia::device::manager::DeviceComponent{
        .parts_count = comp_desc->components[i].parts_count,
        .parts = parts,
    };
    compvec.push_back(dc);
  }

  std::vector<fuchsia::device::manager::DeviceMetadata> metadata = {};
  for (size_t i = 0; i < comp_desc->metadata_count; i++) {
    auto meta = fuchsia::device::manager::DeviceMetadata{
        .key = comp_desc->metadata_list[i].type,
        .data = ::fidl::VectorView(
            reinterpret_cast<uint8_t*>(const_cast<void*>(comp_desc->metadata_list[i].data)),
            comp_desc->metadata_list[i].length)};
    metadata.push_back(meta);
  }

  fuchsia::device::manager::CompositeDeviceDescriptor comp_dev = {
      .props = ::fidl::VectorView(
          reinterpret_cast<uint64_t*>(const_cast<zx_device_prop*>(comp_desc->props)),
          comp_desc->props_count),
      .components = ::fidl::VectorView(compvec),
      .coresident_device_index = comp_desc->coresident_device_index,
      .metadata = ::fidl::VectorView(metadata)};

  log_rpc(dev, "create-composite");
  static_assert(sizeof(comp_desc->props[0]) == sizeof(uint64_t));
  auto response = fuchsia::device::manager::Coordinator::Call::AddCompositeDevice(
      zx::unowned_channel(rpc.get()), ::fidl::StringView(name, strlen(name)), comp_dev);
  zx_status_t status = response.status();
  zx_status_t call_status = ZX_OK;
  if (status == ZX_OK && response.Unwrap()->result.is_err()) {
    call_status = response.Unwrap()->result.err();
  }
  if (status != ZX_OK) {
    return status;
  }
  log_rpc_result("create-composite", status, call_status);
  return call_status;
}

zx_status_t devhost_schedule_work(const fbl::RefPtr<zx_device_t>& dev, void (*callback)(void*),
                                  void* cookie) {
  if (!callback) {
    return ZX_ERR_INVALID_ARGS;
  }
  DevhostCtx().PushWorkItem(dev, [callback, cookie]() { callback(cookie); });
  return ZX_OK;
}

zx_status_t devhost_start_connection(fbl::RefPtr<DevfsConnection> conn, zx::channel h) {
  conn->set_channel(std::move(h));
  return DevfsConnection::BeginWait(std::move(conn), DevhostAsyncLoop()->dispatcher());
}

int device_host_main(int argc, char** argv) {
  root_resource_handle = zx_take_startup_handle(PA_HND(PA_RESOURCE, 0));
  if (root_resource_handle == ZX_HANDLE_INVALID) {
    log(TRACE, "devhost: no root resource handle!\n");
  }

  devhost_io_init();

  log(TRACE, "devhost: main()\n");

  zx::channel root_conn_channel(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
  if (!root_conn_channel.is_valid()) {
    log(ERROR, "devhost: rpc handle invalid\n");
    return -1;
  }

  zx_status_t r;

  if (getenv_bool("driver.tracing.enable", true)) {
    r = devhost_start_trace_provider();
    if (r != ZX_OK) {
      log(INFO, "devhost: error registering as trace provider: %d\n", r);
      // This is not a fatal error.
    }
  }

  r = devhost_connect_scheduler_profile_provider();
  if (r != ZX_OK) {
    log(INFO, "devhost: error connecting to profile provider: %d\n", r);
    return -1;
  }

  if ((r = SetupRootDevcoordinatorConnection(std::move(root_conn_channel))) != ZX_OK) {
    log(ERROR, "devhost: could not watch rpc channel: %d\n", r);
    return -1;
  }

  if (r = DevhostCtx().SetupEventWaiter(); r != ZX_OK) {
    log(ERROR, "devhost: could not setup event watcher: %d\n", r);
    return -1;
  }

  r = DevhostAsyncLoop()->Run(zx::time::infinite(), false /* once */);
  log(ERROR, "devhost: async loop finished: %d\n", r);

  return 0;
}

}  // namespace devmgr

__EXPORT int devmgr_device_host_main(int argc, char** argv) {
  return devmgr::device_host_main(argc, argv);
}
