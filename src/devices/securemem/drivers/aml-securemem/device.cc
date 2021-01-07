// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fuchsia/hardware/composite/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/async/default.h>
#include <zircon/errors.h>
#include <zircon/syscalls/object.h>

#include <array>
#include <cinttypes>
#include <memory>

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>

#include "log.h"
#include "src/devices/securemem/drivers/aml-securemem/aml-securemem-bind.h"

namespace amlogic_secure_mem {

zx_status_t AmlogicSecureMemDevice::Create(void* ctx, zx_device_t* parent) {
  std::unique_ptr<AmlogicSecureMemDevice> sec_mem(new AmlogicSecureMemDevice(parent));

  zx_status_t status = sec_mem->Bind();
  if (status == ZX_OK) {
    // devmgr should now own the lifetime
    __UNUSED auto ptr = sec_mem.release();
  }

  return status;
}

zx_status_t AmlogicSecureMemDevice::Bind() {
  ddk_dispatcher_thread_ = thrd_current();
  ddk_loop_closure_queue_.SetDispatcher(async_get_default_dispatcher(), ddk_dispatcher_thread_);

  zx_status_t status = ZX_OK;

  ddk::CompositeProtocolClient composite(parent());
  if (!composite.is_valid()) {
    LOG(ERROR, "Unable to get composite protocol");
    return status;
  }

  status = ddk::PDevProtocolClient::CreateFromComposite(
      composite, "fuchsia.hardware.platform.device.PDev", &pdev_proto_client_);
  if (status != ZX_OK) {
    LOG(ERROR, "Unable to get pdev protocol - status: %d", status);
    return status;
  }

  status =
      ddk::SysmemProtocolClient::CreateFromComposite(composite, "sysmem", &sysmem_proto_client_);
  if (status != ZX_OK) {
    LOG(ERROR, "Unable to get sysmem protocol - status: %d", status);
    return status;
  }

  status = ddk::TeeProtocolClient::CreateFromComposite(composite, "tee", &tee_proto_client_);
  if (status != ZX_OK) {
    LOG(ERROR, "ddk::TeeProtocolClient::CreateFromDevice() failed - status: %d", status);
    return status;
  }

  // See note on the constraints of |bti_| in the header.
  constexpr uint32_t kBtiIndex = 0;
  status = pdev_proto_client_.GetBti(kBtiIndex, &bti_);
  if (status != ZX_OK) {
    LOG(ERROR, "Unable to get bti handle - status: %d", status);
    return status;
  }

  status = CreateAndServeSysmemTee();
  if (status != ZX_OK) {
    LOG(ERROR, "CreateAndServeSysmemTee() failed - status: %d", status);
    return status;
  }

  status = DdkAdd(kDeviceName);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to add device");
    return status;
  }

  return status;
}

zx_status_t AmlogicSecureMemDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::securemem::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

// TODO(fxbug.dev/36888): Determine if we only ever use mexec to reboot from zedboot into a
// netboot(ed) image. Iff so, we could avoid some complexity here by not loading aml-securemem in
// zedboot, and not handling suspend(mexec) here, and not having UnregisterSecureMem().
void AmlogicSecureMemDevice::DdkSuspend(ddk::SuspendTxn txn) {
  LOG(DEBUG, "aml-securemem: begin DdkSuspend() - Suspend Reason: %d", txn.suspend_reason());

  if ((txn.suspend_reason() & DEVICE_MASK_SUSPEND_REASON) != DEVICE_SUSPEND_REASON_MEXEC) {
    // When a driver doesn't set a suspend function, the default impl returns
    // ZX_OK.
    txn.Reply(ZX_OK, txn.requested_state());
    return;
  }

  // Sysmem loads first (by design, to maximize chance of getting contiguous
  // memory), and aml-securemem depends on sysmem.  This means aml-securemem
  // will suspend before sysmem, so we have aml-securemem clean up secure memory
  // during its suspend (instead of sysmem trying to call aml-securemem after
  // aml-securemem has already suspended).
  ZX_DEBUG_ASSERT((txn.suspend_reason() & DEVICE_MASK_SUSPEND_REASON) ==
                  DEVICE_SUSPEND_REASON_MEXEC);

  if (sysmem_secure_mem_server_) {
    is_suspend_mexec_ = true;

    // We'd like this to be able to suspend async, but instead since DdkSuspend() is a sync call, we
    // have to pump the ddk_loop_closure_queue_ below (so far).
    sysmem_secure_mem_server_->StopAsync();

    // TODO(dustingreen): If DdkSuspend() becomes async, consider not running closures directly
    // here.  Or, if llcpp server binding permits unbind by an owner of the binding without
    // requiring the whole dispatcher to shutdown, consider not running closures directly here.
    while (sysmem_secure_mem_server_) {
      ddk_loop_closure_queue_.RunOneHere();
    }
  }

  LOG(DEBUG, "aml-securemem: end DdkSuspend()");
  txn.Reply(ZX_OK, txn.requested_state());
}

void AmlogicSecureMemDevice::GetSecureMemoryPhysicalAddress(
    zx::vmo secure_mem, GetSecureMemoryPhysicalAddressCompleter::Sync& completer) {
  auto result = GetSecureMemoryPhysicalAddress(std::move(secure_mem));
  if (result.is_error()) {
    completer.Reply(result.error(), static_cast<zx_paddr_t>(0));
  }

  completer.Reply(ZX_OK, result.value());
}

fit::result<zx_paddr_t, zx_status_t> AmlogicSecureMemDevice::GetSecureMemoryPhysicalAddress(
    zx::vmo secure_mem) {
  ZX_DEBUG_ASSERT(secure_mem.is_valid());
  ZX_ASSERT(bti_.is_valid());

  // Validate that the VMO handle passed meets additional constraints.
  zx_info_vmo_t secure_mem_info;
  zx_status_t status = secure_mem.get_info(ZX_INFO_VMO, reinterpret_cast<void*>(&secure_mem_info),
                                           sizeof(secure_mem_info), nullptr, nullptr);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to get VMO info - status: %d", status);
    return fit::error(status);
  }

  // Only allow pinning on VMOs that are contiguous.
  if ((secure_mem_info.flags & ZX_INFO_VMO_CONTIGUOUS) != ZX_INFO_VMO_CONTIGUOUS) {
    LOG(ERROR, "Received non-contiguous VMO type to pin");
    return fit::error(ZX_ERR_WRONG_TYPE);
  }

  // Pin the VMO to get the physical address.
  zx_paddr_t paddr;
  zx::pmt pmt;
  status = bti_.pin(ZX_BTI_CONTIGUOUS | ZX_BTI_PERM_READ, secure_mem, 0 /* offset */,
                    secure_mem_info.size_bytes, &paddr, 1u, &pmt);
  if (status != ZX_OK) {
    LOG(ERROR, "Failed to pin memory - status: %d", status);
    return fit::error(status);
  }

  // Unpinning the PMT should never fail
  status = pmt.unpin();
  ZX_DEBUG_ASSERT(status == ZX_OK);

  return fit::ok(paddr);
}

zx_status_t AmlogicSecureMemDevice::CreateAndServeSysmemTee() {
  ZX_DEBUG_ASSERT(tee_proto_client_.is_valid());
  zx::channel tee_device_client;
  zx::channel tee_device_server;
  zx_status_t status = zx::channel::create(0, &tee_device_client, &tee_device_server);
  if (status != ZX_OK) {
    LOG(ERROR, "optee: failed to create fuchsia.tee.Device channels - status: %d", status);
    return status;
  }
  constexpr zx_handle_t kNoServiceProvider = ZX_HANDLE_INVALID;
  status = tee_proto_client_.Connect(std::move(tee_device_server), zx::channel(kNoServiceProvider));
  if (status != ZX_OK) {
    LOG(ERROR, "optee: tee_client_.Connect() failed - status: %d", status);
    return status;
  }
  sysmem_secure_mem_server_.emplace(ddk_dispatcher_thread_, std::move(tee_device_client));
  zx::channel sysmem_secure_mem_client;
  zx::channel sysmem_secure_mem_server;
  status = zx::channel::create(0, &sysmem_secure_mem_client, &sysmem_secure_mem_server);
  if (status != ZX_OK) {
    LOG(ERROR, "failed to create sysmem tee channels - status: %d", status);
    return status;
  }
  status = sysmem_secure_mem_server_->BindAsync(
      std::move(sysmem_secure_mem_server), &sysmem_secure_mem_server_thread_,
      [this](bool is_success) {
        ZX_DEBUG_ASSERT(thrd_current() == sysmem_secure_mem_server_thread_);
        ddk_loop_closure_queue_.Enqueue([this, is_success] {
          ZX_DEBUG_ASSERT(thrd_current() == ddk_dispatcher_thread_);
          // Else the current lambda wouldn't be running.
          ZX_DEBUG_ASSERT(sysmem_secure_mem_server_);
          if (!is_success) {
            // This unexpected loss of connection to sysmem should never happen.  Complain if it
            // does happen.
            //
            // TODO(dustingreen): Determine if there's a way to cause the aml-securemem's devhost
            // to get re-started cleanly.  Currently this is leaving the overall device in a state
            // where DRM playback will likely be impossible (we should never get here).
            //
            // We may or may not see this message, depending on whether the sysmem failure causes a
            // hard reboot first.
            LOG(ERROR, "fuchsia::sysmem::Tee channel close !is_success - DRM playback will fail");
          } else {
            // If is_success, that means the sysmem_secure_mem_server_ is being shut down
            // intentionally before any channel close.  So far, we only do this for suspend(mexec).
            // In this case, tell sysmem that all is well, before the
            // sysmem_secure_mem_server_.reset() below causes the channel to close (which sysmem
            // would otherwise intentionally interpret as justifying a hard reboot).
            ZX_DEBUG_ASSERT(is_suspend_mexec_);
            LOG(DEBUG, "calling sysmem_proto_client_.UnregisterSecureMem()...");
            zx_status_t status = sysmem_proto_client_.UnregisterSecureMem();
            LOG(DEBUG, "sysmem_proto_client_.UnregisterSecureMem() returned");
            if (status != ZX_OK) {
              // Ignore this failure here, but sysmem may panic if sysmem sees
              // sysmem_secure_mem_server_ channel close without seeing UnregisterSecureMem() first.
              LOG(ERROR,
                  "sysmem_proto_client_.UnregisterSecureMem() failed (ignoring here) - status: %d",
                  status);
            }
          }

          // Regardless of whether this is due to DdkSuspend() or unexpected channel closure, we
          // won't be serving the fuchsia::sysmem::Tee channel any more. The ~SysmemSecureMemServer
          // is designed to be called on the DDK thread.
          //
          // If DdkSuspend() is presently running, this lets it continue.
          sysmem_secure_mem_server_.reset();
          LOG(DEBUG, "Done serving fuchsia::sysmem::Tee");
          // TODO(dustingreen): If DdkSuspend() were async, we could potentially finish the suspend
          // here instead of pumping ddk_loop_closure_queue_ until !sysmem_secure_mem_server_.
          // Similar for an async DdkUnbind() (assuming that ever needs to be handled in this
          // driver).
        });
      });
  if (status != ZX_OK) {
    LOG(ERROR, "sysmem_secure_mem_server_->BindAsync() failed - status: %d", status);
    // When BindAsync() fails, we don't call StopAsync().
    sysmem_secure_mem_server_.reset();
    return status;
  }

  // Tell sysmem about the fidl::sysmem::Tee channel that sysmem will use (async) to configure
  // secure memory ranges.  Sysmem won't fidl call back during this banjo call.
  LOG(DEBUG, "calling sysmem_proto_client_.RegisterSecureMem()...");
  status = sysmem_proto_client_.RegisterSecureMem(std::move(sysmem_secure_mem_client));
  if (status != ZX_OK) {
    // In this case sysmem_secure_mem_server_ will get cleaned up when the channel close is noticed
    // soon.
    LOG(ERROR, "optee: Failed to RegisterSecureMem()");
    return status;
  }
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlogicSecureMemDevice::Create;
  return ops;
}();

}  // namespace amlogic_secure_mem

ZIRCON_DRIVER(amlogic_secure_mem, amlogic_secure_mem::driver_ops, "zircon", "0.1");
