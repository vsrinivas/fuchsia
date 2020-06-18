// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../usb-mass-storage.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <zircon/process.h>

#include <variant>

#include <fbl/array.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <zxtest/zxtest.h>

#include "../block.h"

namespace {

constexpr uint8_t kBlockSize = 5;

// Mock device based on code from ums-function.c

using usb_descriptor = std::variant<usb_interface_descriptor_t, usb_endpoint_descriptor_t>;

const usb_descriptor kDescriptors[] = {
    // Interface descriptor
    usb_interface_descriptor_t{sizeof(usb_descriptor), USB_DT_INTERFACE, 0, 0, 2, 8, 7, 0x50, 0},
    // IN endpoint
    usb_endpoint_descriptor_t{sizeof(usb_descriptor), USB_DT_ENDPOINT, USB_DIR_IN,
                              USB_ENDPOINT_BULK, 64, 0},
    // OUT endpoint
    usb_endpoint_descriptor_t{sizeof(usb_descriptor), USB_DT_ENDPOINT, USB_DIR_OUT,
                              USB_ENDPOINT_BULK, 64, 0}};

struct Packet;
struct Packet : fbl::DoublyLinkedListable<fbl::RefPtr<Packet>>, fbl::RefCounted<Packet> {
  explicit Packet(fbl::Array<unsigned char>&& source) { data = std::move(source); }
  explicit Packet() { stall = true; }
  bool stall = false;
  fbl::Array<unsigned char> data;
};

class FakeTimer : public ums::WaiterInterface {
 public:
  zx_status_t Wait(sync_completion_t* completion, zx_duration_t duration) override {
    return timeout_handler_(completion, duration);
  }

  void set_timeout_handler(fit::function<zx_status_t(sync_completion_t*, zx_duration_t)> handler) {
    timeout_handler_ = std::move(handler);
  }

 private:
  fit::function<zx_status_t(sync_completion_t*, zx_duration_t)> timeout_handler_;
};

enum ErrorInjection {
  NoFault,
  RejectCacheCbw,
  RejectCacheDataStage,
};

constexpr auto kInitialTagValue = 8;

struct Context {
  Context* parent;
  ums::UmsBlockDevice* block_device;
  ums::UsbMassStorageDevice* ums_device;
  uint32_t desired_proto;
  usb_protocol_t proto;
  fbl::DoublyLinkedList<fbl::RefPtr<Packet>> pending_packets;
  ums_csw_t csw;
  const usb_descriptor* descs;
  size_t desc_length;
  size_t block_devs;
  ums::UmsBlockDevice* devices[4];
  sync_completion_t completion;
  zx_status_t status;
  block_op_t* op;
  uint64_t transfer_offset;
  uint64_t transfer_blocks;
  uint8_t transfer_type;
  uint8_t transfer_lun;
  size_t pending_write;
  ErrorInjection failure_mode;
  fbl::RefPtr<Packet> last_transfer;
  uint32_t tag = kInitialTagValue;
};

class Binder : public fake_ddk::Bind {
 public:
  zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id, void* protocol) {
    auto context = reinterpret_cast<const Context*>(device);
    if (proto_id == context->desired_proto) {
      *reinterpret_cast<usb_protocol_t*>(protocol) = context->proto;
      return ZX_OK;
    }
    return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
  }
  zx_status_t DeviceRemove(zx_device_t* device) {
    Context* ctx = reinterpret_cast<Context*>(device);
    delete ctx;
    return ZX_OK;
  }
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) {
    Context* context = reinterpret_cast<Context*>(parent);
    if (context->parent) {
      Context* parent = context->parent;
      parent->devices[parent->block_devs] = reinterpret_cast<ums::UmsBlockDevice*>(args->ctx);
      if ((++parent->block_devs) == 3) {
        sync_completion_signal(&parent->completion);
      }
    }
    Context* outctx = new Context();
    outctx->parent = context;
    outctx->block_device = reinterpret_cast<ums::UmsBlockDevice*>(args->ctx);
    *out = reinterpret_cast<zx_device_t*>(outctx);
    return ZX_OK;
  }
};
static size_t GetDescriptorLength(void* ctx) {
  Context* context = reinterpret_cast<Context*>(ctx);
  return context->desc_length;
}

static void GetDescriptors(void* ctx, void* buffer, size_t size, size_t* outsize) {
  Context* context = reinterpret_cast<Context*>(ctx);
  *outsize = context->desc_length > size ? size : context->desc_length;
  memcpy(buffer, context->descs, *outsize);
}

static zx_status_t ControlIn(void* ctx, uint8_t request_type, uint8_t request, uint16_t value,
                             uint16_t index, int64_t timeout, void* out_read_buffer,
                             size_t read_size, size_t* out_read_actual) {
  switch (request) {
    case USB_REQ_GET_MAX_LUN: {
      if (!read_size) {
        *out_read_actual = 0;
        return ZX_OK;
      }
      *reinterpret_cast<unsigned char*>(out_read_buffer) = 3;
      *out_read_actual = 1;
      return ZX_OK;
    }
    default:
      return ZX_ERR_IO_REFUSED;
  }
}

static size_t GetMaxTransferSize(void* ctx, uint8_t ep) {
  switch (ep) {
    case USB_DIR_OUT:
      __FALLTHROUGH;
    case USB_DIR_IN:
      // 10MB transfer size (to test large transfers)
      // (is this even possible in real hardware?)
      return 1000 * 1000 * 10;
    default:
      return 0;
  }
}
static size_t GetRequestSize(void* ctx) { return sizeof(usb_request_t); }
static void RequestQueue(void* ctx, usb_request_t* usb_request,
                         const usb_request_complete_t* complete_cb) {
  Context* context = reinterpret_cast<Context*>(ctx);
  if (context->pending_write) {
    void* data;
    usb_request_mmap(usb_request, &data);
    memcpy(context->last_transfer->data.data(), data, context->pending_write);
    context->pending_write = 0;
    usb_request->response.status = ZX_OK;
    complete_cb->callback(complete_cb->ctx, usb_request);
    return;
  }
  if ((usb_request->header.ep_address & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_IN) {
    if (context->pending_packets.begin() == context->pending_packets.end()) {
      usb_request->response.status = ZX_OK;
      complete_cb->callback(complete_cb->ctx, usb_request);
    } else {
      auto packet = context->pending_packets.pop_front();
      if (packet->stall) {
        usb_request->response.actual = 0;
        usb_request->response.status = ZX_ERR_IO_REFUSED;
        complete_cb->callback(complete_cb->ctx, usb_request);
        return;
      }
      size_t len =
          usb_request->size < packet->data.size() ? usb_request->size : packet->data.size();
      usb_request_copy_to(usb_request, packet->data.data(), len, 0);
      usb_request->response.actual = len;
      usb_request->response.status = ZX_OK;
      complete_cb->callback(complete_cb->ctx, usb_request);
    }
    return;
  }
  void* data;
  usb_request_mmap(usb_request, &data);
  uint32_t header;
  memcpy(&header, data, sizeof(header));
  header = le32toh(header);
  switch (header) {
    case CBW_SIGNATURE: {
      ums_cbw_t cbw;
      memcpy(&cbw, data, sizeof(cbw));
      if (cbw.bCBWLUN > 3) {
        usb_request->response.status = ZX_OK;
        complete_cb->callback(complete_cb->ctx, usb_request);
        return;
      }
      auto DataTransfer = [&]() {
        if ((context->transfer_offset == cbw.bCBWLUN) &&
            (context->transfer_blocks == (cbw.bCBWLUN + 1U) * 65534U)) {
          uint8_t opcode = cbw.CBWCB[0];
          size_t transfer_length = context->transfer_blocks * kBlockSize;
          fbl::Array<unsigned char> transfer(new unsigned char[transfer_length], transfer_length);
          context->last_transfer = fbl::MakeRefCounted<Packet>(std::move(transfer));
          context->transfer_lun = cbw.bCBWLUN;
          if ((opcode == UMS_READ10) || (opcode == UMS_READ12) || (opcode == UMS_READ16)) {
            // Push reply
            context->pending_packets.push_back(context->last_transfer);
          } else {
            if ((opcode == UMS_WRITE10) || (opcode == UMS_WRITE12) || (opcode == UMS_WRITE16)) {
              context->pending_write = transfer_length;
            }
          }
          // Push CSW
          fbl::Array<unsigned char> csw(new unsigned char[sizeof(ums_csw_t)], sizeof(ums_csw_t));
          context->csw.dCSWDataResidue = 0;
          context->csw.dCSWTag = context->tag++;
          context->csw.bmCSWStatus = CSW_SUCCESS;
          memcpy(csw.data(), &context->csw, sizeof(context->csw));
          context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(csw)));
        }
      };
      switch (cbw.CBWCB[0]) {
        case UMS_WRITE16:
          __FALLTHROUGH;
        case UMS_READ16: {
          scsi_command16_t cmd;
          memcpy(&cmd, cbw.CBWCB, sizeof(cmd));
          context->transfer_blocks = be32toh(cmd.length);
          context->transfer_offset = be64toh(cmd.lba);
          context->transfer_type = cbw.CBWCB[0];
          DataTransfer();
          usb_request->response.status = ZX_OK;
          complete_cb->callback(complete_cb->ctx, usb_request);
          break;
        }
        case UMS_WRITE12:
          __FALLTHROUGH;
        case UMS_READ12: {
          scsi_command12_t cmd;
          memcpy(&cmd, cbw.CBWCB, sizeof(cmd));
          context->transfer_blocks = be32toh(cmd.length);
          context->transfer_offset = be32toh(cmd.lba);
          context->transfer_type = cbw.CBWCB[0];
          DataTransfer();
          usb_request->response.status = ZX_OK;
          complete_cb->callback(complete_cb->ctx, usb_request);
          break;
        }
        case UMS_WRITE10:
          __FALLTHROUGH;
        case UMS_READ10: {
          scsi_command10_t cmd;
          memcpy(&cmd, cbw.CBWCB, sizeof(cmd));
          context->transfer_blocks =
              static_cast<uint16_t>(cmd.length_lo) | (static_cast<uint16_t>(cmd.length_hi) << 8);
          context->transfer_offset = be32toh(cmd.lba);
          context->transfer_type = cbw.CBWCB[0];
          DataTransfer();
          usb_request->response.status = ZX_OK;
          complete_cb->callback(complete_cb->ctx, usb_request);
          break;
        }
        case UMS_SYNCHRONIZE_CACHE: {
          // Push CSW
          fbl::Array<unsigned char> csw(new unsigned char[sizeof(ums_csw_t)], sizeof(ums_csw_t));
          context->csw.dCSWDataResidue = 0;
          context->csw.dCSWTag = context->tag++;
          context->csw.bmCSWStatus = CSW_SUCCESS;
          context->transfer_lun = cbw.bCBWLUN;
          context->transfer_type = cbw.CBWCB[0];
          memcpy(csw.data(), &context->csw, sizeof(context->csw));
          context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(csw)));
          usb_request->response.status = ZX_OK;
          complete_cb->callback(complete_cb->ctx, usb_request);
          break;
        }
        case UMS_INQUIRY: {
          scsi_command6_t cmd;
          memcpy(&cmd, cbw.CBWCB, sizeof(cmd));
          if (cmd.length == UMS_INQUIRY_TRANSFER_LENGTH) {
            // Push reply
            fbl::Array<unsigned char> reply(new unsigned char[UMS_INQUIRY_TRANSFER_LENGTH],
                                            UMS_INQUIRY_TRANSFER_LENGTH);
            reply[0] = 0;     // Peripheral Device Type: Direct access block device
            reply[1] = 0x80;  // Removable
            reply[2] = 6;     // Version SPC-4
            reply[3] = 0x12;  // Response Data Format
            memcpy(reply.data() + 8, "Google  ", 8);
            memcpy(reply.data() + 16, "Zircon UMS      ", 16);
            memcpy(reply.data() + 32, "1.00", 4);
            memset(reply.data(), 0, UMS_INQUIRY_TRANSFER_LENGTH);
            context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(reply)));
            // Push CSW
            fbl::Array<unsigned char> csw(new unsigned char[sizeof(ums_csw_t)], sizeof(ums_csw_t));
            context->csw.dCSWDataResidue = 0;
            context->csw.dCSWTag = context->tag++;
            context->csw.bmCSWStatus = CSW_SUCCESS;
            memcpy(csw.data(), &context->csw, sizeof(context->csw));
            context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(csw)));
          }
          usb_request->response.status = ZX_OK;
          complete_cb->callback(complete_cb->ctx, usb_request);
          break;
        }
        case UMS_TEST_UNIT_READY: {
          // Push CSW
          fbl::Array<unsigned char> csw(new unsigned char[sizeof(ums_csw_t)], sizeof(ums_csw_t));
          context->csw.dCSWDataResidue = 0;
          context->csw.dCSWTag = context->tag++;
          context->csw.bmCSWStatus = CSW_SUCCESS;
          memcpy(csw.data(), &context->csw, sizeof(context->csw));
          context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(csw)));
          usb_request->response.status = ZX_OK;
          complete_cb->callback(complete_cb->ctx, usb_request);
          break;
        }
        case UMS_READ_CAPACITY16: {
          if (cbw.bCBWLUN == 3) {
            // Push reply
            fbl::Array<unsigned char> reply(new unsigned char[sizeof(scsi_read_capacity_16_t)],
                                            sizeof(scsi_read_capacity_16_t));
            scsi_read_capacity_16_t scsi;
            scsi.block_length = htobe32(kBlockSize);
            scsi.lba = htobe64((976562L * (1 + cbw.bCBWLUN)) + UINT32_MAX);
            memcpy(reply.data(), &scsi, sizeof(scsi));
            context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(reply)));
            // Push CSW
            fbl::Array<unsigned char> csw(new unsigned char[sizeof(ums_csw_t)], sizeof(ums_csw_t));
            context->csw.dCSWDataResidue = 0;
            context->csw.dCSWTag = context->tag++;
            context->csw.bmCSWStatus = CSW_SUCCESS;
            memcpy(csw.data(), &context->csw, sizeof(context->csw));
            context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(csw)));
          }
          usb_request->response.status = ZX_OK;
          complete_cb->callback(complete_cb->ctx, usb_request);
          break;
        }
        case UMS_READ_CAPACITY10: {
          // Push reply
          fbl::Array<unsigned char> reply(new unsigned char[sizeof(scsi_read_capacity_10_t)],
                                          sizeof(scsi_read_capacity_10_t));
          scsi_read_capacity_10_t scsi;
          scsi.block_length = htobe32(kBlockSize);
          scsi.lba = htobe32(976562 * (1 + cbw.bCBWLUN));
          if (cbw.bCBWLUN == 3) {
            scsi.lba = -1;
          }
          memcpy(reply.data(), &scsi, sizeof(scsi));
          context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(reply)));
          // Push CSW
          fbl::Array<unsigned char> csw(new unsigned char[sizeof(ums_csw_t)], sizeof(ums_csw_t));
          context->csw.dCSWDataResidue = 0;
          context->csw.dCSWTag = context->tag++;
          context->csw.bmCSWStatus = CSW_SUCCESS;
          memcpy(csw.data(), &context->csw, sizeof(context->csw));
          context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(csw)));
          usb_request->response.status = ZX_OK;
          complete_cb->callback(complete_cb->ctx, usb_request);
          break;
        }
        case UMS_MODE_SENSE6: {
          scsi_mode_sense_6_command_t cmd;
          memcpy(&cmd, cbw.CBWCB, sizeof(cmd));
          // Push reply
          switch (cmd.page) {
            case 0x3F: {
              fbl::Array<unsigned char> reply(new unsigned char[sizeof(scsi_read_capacity_10_t)],
                                              sizeof(scsi_read_capacity_10_t));
              scsi_mode_sense_6_data_t scsi = {};
              memcpy(reply.data(), &scsi, sizeof(scsi));
              context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(reply)));
              // Push CSW
              fbl::Array<unsigned char> csw(new unsigned char[sizeof(ums_csw_t)],
                                            sizeof(ums_csw_t));
              context->csw.dCSWDataResidue = 0;
              context->csw.dCSWTag = context->tag++;
              context->csw.bmCSWStatus = CSW_SUCCESS;
              memcpy(csw.data(), &context->csw, sizeof(context->csw));
              context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(csw)));
              usb_request->response.status = ZX_OK;
              complete_cb->callback(complete_cb->ctx, usb_request);
              break;
            }
            case 0x08: {
              if (context->failure_mode == RejectCacheCbw) {
                usb_request->response.status = ZX_ERR_IO_REFUSED;
                usb_request->response.actual = 0;
                complete_cb->callback(complete_cb->ctx, usb_request);
                return;
              }
              fbl::Array<unsigned char> reply(new unsigned char[20], 20);
              memset(reply.data(), 0, 20);
              reply[6] = 1 << 2;
              if (context->failure_mode == RejectCacheDataStage) {
                complete_cb->callback(complete_cb->ctx, usb_request);
                context->pending_packets.push_back(fbl::MakeRefCounted<Packet>());
                return;
              } else {
                context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(reply)));
              }
              // Push CSW
              fbl::Array<unsigned char> csw(new unsigned char[sizeof(ums_csw_t)],
                                            sizeof(ums_csw_t));
              context->csw.dCSWDataResidue = 0;
              context->csw.dCSWTag = context->tag++;
              context->csw.bmCSWStatus = CSW_SUCCESS;
              memcpy(csw.data(), &context->csw, sizeof(context->csw));
              context->pending_packets.push_back(fbl::MakeRefCounted<Packet>(std::move(csw)));
              usb_request->response.status = ZX_OK;
              complete_cb->callback(complete_cb->ctx, usb_request);
              break;
            }
            default:
              usb_request->response.status = ZX_OK;
              complete_cb->callback(complete_cb->ctx, usb_request);
          }
          break;
        }
      }
      break;
    }
    default:
      context->csw.bmCSWStatus = CSW_FAILED;
      usb_request->response.status = ZX_ERR_IO;
      complete_cb->callback(complete_cb->ctx, usb_request);
  }
}
static void CompletionCallback(void* ctx, zx_status_t status, block_op_t* op) {
  Context* context = reinterpret_cast<Context*>(ctx);
  context->status = status;
  context->op = op;
  sync_completion_signal(&context->completion);
}

static zx_status_t Setup(Context* context, ums::UsbMassStorageDevice* dev, usb_protocol_ops_t* ops,
                         ErrorInjection inject_failure = NoFault) {
  zx_status_t status = ZX_OK;
  // Device paramaters for physical (parent) device
  context->failure_mode = inject_failure;
  context->parent = nullptr;
  context->ums_device = dev;
  context->block_devs = 0;
  context->pending_write = 0;
  context->csw.dCSWSignature = htole32(CSW_SIGNATURE);
  context->csw.bmCSWStatus = CSW_SUCCESS;
  context->descs = kDescriptors;
  context->desc_length = sizeof(kDescriptors);
  context->desired_proto = ZX_PROTOCOL_USB;
  // Binding of ops to enable communication between the virtual device and UMS driver

  context->proto.ctx = context;
  context->proto.ops = ops;
  ops->get_descriptors_length = GetDescriptorLength;
  ops->get_descriptors = GetDescriptors;
  ops->get_request_size = GetRequestSize;
  ops->request_queue = RequestQueue;
  ops->get_max_transfer_size = GetMaxTransferSize;
  ops->control_in = ControlIn;
  // Driver initialization
  status = dev->Init(true /* is_test_mode */);
  if (status != ZX_OK) {
    return status;
  }
  sync_completion_wait(&context->completion, ZX_TIME_INFINITE);
  sync_completion_reset(&context->completion);
  return status;
}

// UMS read test
// This test validates the read functionality on multiple LUNS
// of a USB mass storage device.
TEST(Ums, TestRead) {
  // Setup
  Binder bind;
  Context parent_dev;
  usb_protocol_ops_t ops;
  auto timer = fbl::MakeRefCounted<FakeTimer>();
  bool has_zero_duration = false;
  timer->set_timeout_handler([&](sync_completion_t* completion, zx_duration_t duration) {
    if (duration == 0) {
      has_zero_duration = true;
    }
    if (duration == ZX_TIME_INFINITE) {
      return sync_completion_wait(completion, duration);
    }
    return ZX_OK;
  });
  ums::UsbMassStorageDevice dev(timer, reinterpret_cast<zx_device_t*>(&parent_dev));
  Setup(&parent_dev, &dev, &ops);
  zx_handle_t vmo;
  uint64_t size;
  zx_vaddr_t mapped;

  // VMO creation to read data into
  EXPECT_EQ(ZX_OK, zx_vmo_create(1000 * 1000 * 10, 0, &vmo), "Failed to create VMO");
  EXPECT_EQ(ZX_OK, zx_vmo_get_size(vmo, &size), "Failed to get size of VMO");
  EXPECT_EQ(ZX_OK, zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, size, &mapped),
            "Failed to map VMO");
  // Perform read transactions
  for (uint32_t i = 0; i < parent_dev.block_devs; i++) {
    ums::Transaction transaction;
    transaction.op.command = BLOCK_OP_READ;
    transaction.op.rw.offset_dev = i;
    transaction.op.rw.length = (i + 1) * 65534;
    transaction.op.rw.offset_vmo = 0;
    transaction.op.rw.vmo = vmo;
    transaction.cookie = &parent_dev;
    transaction.dev = parent_dev.devices[i];
    transaction.completion_cb = CompletionCallback;
    dev.QueueTransaction(&transaction);
    sync_completion_wait(&parent_dev.completion, ZX_TIME_INFINITE);
    sync_completion_reset(&parent_dev.completion);
    uint8_t xfer_type = i == 0 ? UMS_READ10 : i == 3 ? UMS_READ16 : UMS_READ12;
    EXPECT_EQ(i, parent_dev.transfer_lun);
    EXPECT_EQ(xfer_type, parent_dev.transfer_type);
    EXPECT_EQ(0, memcmp(reinterpret_cast<void*>(mapped), parent_dev.last_transfer->data.data(),
                        parent_dev.last_transfer->data.size()));
  }
  // Unbind
  dev.DdkUnbindDeprecated();
  EXPECT_EQ(4, parent_dev.block_devs);
  ASSERT_FALSE(has_zero_duration);
}

// This test validates the write functionality on multiple LUNS
// of a USB mass storage device.
TEST(Ums, TestWrite) {
  // Setup
  Binder bind;
  Context parent_dev;
  usb_protocol_ops_t ops;
  auto timer = fbl::MakeRefCounted<FakeTimer>();
  bool has_zero_duration = false;
  timer->set_timeout_handler([&](sync_completion_t* completion, zx_duration_t duration) {
    if (duration == 0) {
      has_zero_duration = true;
    }
    if (duration == ZX_TIME_INFINITE) {
      return sync_completion_wait(completion, duration);
    }
    return ZX_OK;
  });
  ums::UsbMassStorageDevice dev(timer, reinterpret_cast<zx_device_t*>(&parent_dev));
  Setup(&parent_dev, &dev, &ops);
  zx_handle_t vmo;
  uint64_t size;
  zx_vaddr_t mapped;

  // VMO creation to transfer from
  EXPECT_EQ(ZX_OK, zx_vmo_create(1000 * 1000 * 10, 0, &vmo), "Failed to create VMO");
  EXPECT_EQ(ZX_OK, zx_vmo_get_size(vmo, &size), "Failed to get size of VMO");
  EXPECT_EQ(ZX_OK,
            zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size,
                        &mapped),
            "Failed to map VMO");
  // Add "entropy" for write operation
  for (size_t i = 0; i < size / sizeof(size_t); i++) {
    reinterpret_cast<size_t*>(mapped)[i] = i;
  }
  // Perform write transactions
  for (uint32_t i = 0; i < parent_dev.block_devs; i++) {
    ums::Transaction transaction;
    transaction.op.command = BLOCK_OP_WRITE;
    transaction.op.rw.offset_dev = i;
    transaction.op.rw.length = (i + 1) * 65534;
    transaction.op.rw.offset_vmo = 0;
    transaction.op.rw.vmo = vmo;
    transaction.cookie = &parent_dev;
    transaction.dev = parent_dev.devices[i];
    transaction.completion_cb = CompletionCallback;
    dev.QueueTransaction(&transaction);
    sync_completion_wait(&parent_dev.completion, ZX_TIME_INFINITE);
    sync_completion_reset(&parent_dev.completion);
    uint8_t xfer_type = i == 0 ? UMS_WRITE10 : i == 3 ? UMS_WRITE16 : UMS_WRITE12;
    EXPECT_EQ(i, parent_dev.transfer_lun);
    EXPECT_EQ(xfer_type, parent_dev.transfer_type);
    EXPECT_EQ(0, memcmp(reinterpret_cast<void*>(mapped), parent_dev.last_transfer->data.data(),
                        transaction.op.rw.length * kBlockSize));
  }
  // Unbind
  dev.DdkUnbindDeprecated();
  ASSERT_FALSE(has_zero_duration);
  EXPECT_EQ(4, parent_dev.block_devs);
}

// This test validates the flush functionality on multiple LUNS
// of a USB mass storage device.
TEST(Ums, TestFlush) {
  // Setup
  Binder bind;
  Context parent_dev;
  auto timer = fbl::MakeRefCounted<FakeTimer>();
  bool has_zero_duration = false;
  timer->set_timeout_handler([&](sync_completion_t* completion, zx_duration_t duration) {
    if (duration == 0) {
      has_zero_duration = true;
    }
    // Wait for the sync_completion_t if given an infinite timeout
    // (infinite timeouts are used for synchronization)
    if (duration == ZX_TIME_INFINITE) {
      return sync_completion_wait(completion, duration);
    }
    return ZX_OK;
  });
  ums::UsbMassStorageDevice dev(timer, reinterpret_cast<zx_device_t*>(&parent_dev));
  usb_protocol_ops_t ops;
  Setup(&parent_dev, &dev, &ops);

  // Perform flush transactions
  for (uint32_t i = 0; i < parent_dev.block_devs; i++) {
    ums::Transaction transaction;
    transaction.op.command = BLOCK_OP_FLUSH;
    transaction.cookie = &parent_dev;
    transaction.dev = parent_dev.devices[i];
    transaction.completion_cb = CompletionCallback;
    dev.QueueTransaction(&transaction);
    sync_completion_wait(&parent_dev.completion, ZX_TIME_INFINITE);
    sync_completion_reset(&parent_dev.completion);
    uint8_t xfer_type = UMS_SYNCHRONIZE_CACHE;
    EXPECT_EQ(i, parent_dev.transfer_lun);
    EXPECT_EQ(xfer_type, parent_dev.transfer_type);
  }
  // Unbind
  dev.DdkUnbindDeprecated();
  ASSERT_FALSE(has_zero_duration);
  EXPECT_EQ(4, parent_dev.block_devs);
}

TEST(Ums, CbwStallDoesNotFreezeDriver) {
  // Setup
  Binder bind;
  Context parent_dev;
  auto timer = fbl::MakeRefCounted<FakeTimer>();
  bool has_zero_duration = false;
  timer->set_timeout_handler([&](sync_completion_t* completion, zx_duration_t duration) {
    if (duration == 0) {
      has_zero_duration = true;
    }
    // Wait for the sync_completion_t if given an infinite timeout
    // (infinite timeouts are used for synchronization)
    if (duration == ZX_TIME_INFINITE) {
      return sync_completion_wait(completion, duration);
    }
    return ZX_OK;
  });
  ums::UsbMassStorageDevice dev(timer, reinterpret_cast<zx_device_t*>(&parent_dev));
  usb_protocol_ops_t ops;
  Setup(&parent_dev, &dev, &ops, RejectCacheCbw);
  // Unbind
  dev.DdkUnbindDeprecated();
  ASSERT_FALSE(has_zero_duration);
  EXPECT_EQ(4, parent_dev.block_devs);
}

TEST(Ums, DataStageStallDoesNotFreezeDriver) {
  // Setup
  Binder bind;
  Context parent_dev;
  auto timer = fbl::MakeRefCounted<FakeTimer>();
  bool has_zero_duration = false;
  timer->set_timeout_handler([&](sync_completion_t* completion, zx_duration_t duration) {
    if (duration == 0) {
      has_zero_duration = true;
    }
    // Wait for the sync_completion_t if given an infinite timeout
    // (infinite timeouts are used for synchronization)
    if (duration == ZX_TIME_INFINITE) {
      return sync_completion_wait(completion, duration);
    }
    return ZX_OK;
  });
  ums::UsbMassStorageDevice dev(timer, reinterpret_cast<zx_device_t*>(&parent_dev));
  usb_protocol_ops_t ops;
  Setup(&parent_dev, &dev, &ops, RejectCacheDataStage);
  // Unbind
  dev.DdkUnbindDeprecated();
  ASSERT_FALSE(has_zero_duration);
  EXPECT_EQ(4, parent_dev.block_devs);
}

}  // namespace
