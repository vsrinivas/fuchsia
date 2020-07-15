// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl-async-2/fidl_struct.h>

#include <ddktl/protocol/platform/bus.h>
#include <src/devices/sysmem/drivers/sysmem/device.h>
#include <src/devices/sysmem/drivers/sysmem/driver.h>

using BufferCollectionConstraints = FidlStruct<fuchsia_sysmem_BufferCollectionConstraints,
                                               llcpp::fuchsia::sysmem::BufferCollectionConstraints>;
using BufferCollectionInfo = FidlStruct<fuchsia_sysmem_BufferCollectionInfo_2,
                                        llcpp::fuchsia::sysmem::BufferCollectionInfo_2>;

#define DBGRTN 0

#define LOGRTN(status, ...)           \
  {                                   \
    if (status != ZX_OK) {            \
      if (DBGRTN) {                   \
        fprintf(stderr, __VA_ARGS__); \
        fflush(stderr);               \
      }                               \
      return 0;                       \
    }                                 \
  }
#define LOGRTNC(condition, ...)       \
  {                                   \
    if ((condition)) {                \
      if (DBGRTN) {                   \
        fprintf(stderr, __VA_ARGS__); \
        fflush(stderr);               \
      }                               \
      return 0;                       \
    }                                 \
  }

namespace {

class FakePBus : public ddk::PBusProtocol<FakePBus, ddk::base_protocol> {
 public:
  FakePBus() : proto_({&pbus_protocol_ops_, this}) {}
  const pbus_protocol_t* proto() const { return &proto_; }
  zx_status_t PBusDeviceAdd(const pbus_dev_t* dev) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusRegisterProtocol(uint32_t proto_id, const void* protocol, size_t protocol_size) {
    return ZX_OK;
  }
  zx_status_t PBusGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusSetBootloaderInfo(const pbus_bootloader_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusCompositeDeviceAdd(const pbus_dev_t* dev, const device_fragment_t* fragments_list,
                                     size_t fragments_count, uint32_t coresident_device_index) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusRegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cbin) {
    return ZX_ERR_NOT_SUPPORTED;
  }

 private:
  pbus_protocol_t proto_;
};

class FakePDev : public ddk::PDevProtocol<FakePDev, ddk::base_protocol> {
 public:
  FakePDev() : proto_({&pdev_protocol_ops_, this}) {}

  const pdev_protocol_t* proto() const { return &proto_; }

  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti) {
    return fake_bti_create(out_bti->reset_and_get_address());
  }

  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

 private:
  pdev_protocol_t proto_;
};

class FakeDdkSysmem {
 public:
  ~FakeDdkSysmem() {
    if (initialized_) {
      sysmem_.DdkAsyncRemove();
      initialized_ = false;
    }
  }
  fake_ddk::Bind& ddk() { return ddk_; }

  bool Init() {
    if (initialized_) {
      fprintf(stderr, "FakeDdkSysmem already initialized.\n");
      fflush(stderr);
      return false;
    }
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[2], 2);
    protocols[0] = {ZX_PROTOCOL_PBUS, *reinterpret_cast<const fake_ddk::Protocol*>(pbus_.proto())};
    protocols[1] = {ZX_PROTOCOL_PDEV, *reinterpret_cast<const fake_ddk::Protocol*>(pdev_.proto())};
    ddk_.SetProtocols(std::move(protocols));
    if (ZX_OK == sysmem_.Bind()) {
      initialized_ = true;
    }
    return initialized_;
  }

 protected:
  bool initialized_ = false;
  sysmem_driver::Driver sysmem_ctx_;
  sysmem_driver::Device sysmem_{fake_ddk::kFakeParent, &sysmem_ctx_};

  FakePBus pbus_;
  FakePDev pdev_;
  // ddk must be destroyed before sysmem because it may be executing messages against sysmem on
  // another thread.
  fake_ddk::Bind ddk_;
};

zx_status_t connect_to_sysmem_driver(zx_handle_t fake_ddk_client,
                                     zx::channel* allocator_client_param) {
  zx_status_t status;

  zx::channel allocator_client;
  zx::channel allocator_server;
  status = zx::channel::create(0, &allocator_client, &allocator_server);
  LOGRTN(status, "Failed allocator channel create.\n");

  status = fuchsia_sysmem_DriverConnectorConnect(fake_ddk_client, allocator_server.release());
  LOGRTN(status, "Failed sysmem driver connect.\n");

  *allocator_client_param = std::move(allocator_client);
  return ZX_OK;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(uint8_t* data, size_t size) {
  const size_t kRequiredFuzzingBytes = sizeof(fuchsia_sysmem_BufferCollectionConstraints);

  LOGRTNC(size != kRequiredFuzzingBytes, "size: %zu != kRequiredFuzzingBytes: %zu\n", size,
          kRequiredFuzzingBytes);
  FakeDdkSysmem fake_sysmem;
  LOGRTNC(!fake_sysmem.Init(), "Failed FakeDdkSysmem::Init()\n");

  zx::channel allocator_client;
  zx_status_t status =
      connect_to_sysmem_driver(fake_sysmem.ddk().FidlClient().get(), &allocator_client);
  LOGRTN(status, "Failed to connect to sysmem driver.\n");

  zx::channel token_server, token_client;
  status = zx::channel::create(0u, &token_server, &token_client);
  LOGRTN(status, "Failed token channel create.\n");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  LOGRTN(status, "Failed to allocate shared collection.\n");

  zx::channel collection_server, collection_client;
  status = zx::channel::create(0, &collection_client, &collection_server);
  LOGRTN(status, "Failed collection channel create.\n");

  LOGRTNC(token_client.get() == ZX_HANDLE_INVALID, "Invalid token_client handle.\n");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client.get(), token_client.release(), collection_server.release());
  LOGRTN(status, "Failed to bind shared collection.\n");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  memcpy(constraints.get(), data, kRequiredFuzzingBytes);

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  LOGRTN(status, "Failed to set buffer collection constraints.\n");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  LOGRTN(status, "Failed on WaitForBuffersAllocated.\n");
  LOGRTN(allocation_status, "Bad allocation_status on WaitForBuffersAllocated.\n");

  return 0;
}
