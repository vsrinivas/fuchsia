// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <limits.h>
#include <sys/types.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/iommu.h>
#include <zircon/syscalls/resource.h>
#include <zircon/threads.h>

#include <acpica/acpi.h>
#include <ddk/debug.h>
#include <ddk/protocol/acpi.h>
#include <ddk/protocol/pciroot.h>
#include <ddk/protocol/sysmem.h>

#include "acpi-private.h"
#include "dev.h"
#include "errors.h"
#include "iommu.h"
#include "methods.h"
#include "nhlt.h"
#include "pci.h"
#include "power.h"
#include "resources.h"
#include "sysmem.h"

static void acpi_device_release(void* ctx) {
  acpi_device_t* dev = (acpi_device_t*)ctx;
  free(dev);
}

static zx_protocol_device_t acpi_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = acpi_device_release,
};

typedef struct {
  acpi_device_pio_resource_t* pio_resources;
  size_t pio_resource_count;
  size_t pio_resource_i;

  acpi_device_mmio_resource_t* mmio_resources;
  size_t mmio_resource_count;
  size_t mmio_resource_i;

  acpi_device_irq_t* irqs;
  size_t irq_count;
  size_t irq_i;
} acpi_crs_ctx_t;

static ACPI_STATUS report_current_resources_resource_cb(ACPI_RESOURCE* res, void* _ctx) {
  acpi_crs_ctx_t* ctx = (acpi_crs_ctx_t*)_ctx;

  if (resource_is_memory(res)) {
    resource_memory_t mem;
    zx_status_t st = resource_parse_memory(res, &mem);
    // only expect fixed memory resource. resource_parse_memory sets minimum == maximum
    // for this memory resource type.
    if ((st != ZX_OK) || (mem.minimum != mem.maximum)) {
      return AE_ERROR;
    }

    ctx->mmio_resources[ctx->mmio_resource_i].writeable = mem.writeable;
    ctx->mmio_resources[ctx->mmio_resource_i].base_address = mem.minimum;
    ctx->mmio_resources[ctx->mmio_resource_i].alignment = mem.alignment;
    ctx->mmio_resources[ctx->mmio_resource_i].address_length = mem.address_length;

    ctx->mmio_resource_i += 1;

  } else if (resource_is_address(res)) {
    resource_address_t addr;
    zx_status_t st = resource_parse_address(res, &addr);
    if (st != ZX_OK) {
      return AE_ERROR;
    }
    if ((addr.resource_type == RESOURCE_ADDRESS_MEMORY) && addr.min_address_fixed &&
        addr.max_address_fixed && (addr.maximum < addr.minimum)) {
      ctx->mmio_resources[ctx->mmio_resource_i].writeable = true;
      ctx->mmio_resources[ctx->mmio_resource_i].base_address = addr.min_address_fixed;
      ctx->mmio_resources[ctx->mmio_resource_i].alignment = 0;
      ctx->mmio_resources[ctx->mmio_resource_i].address_length = addr.address_length;

      ctx->mmio_resource_i += 1;
    }

  } else if (resource_is_io(res)) {
    resource_io_t io;
    zx_status_t st = resource_parse_io(res, &io);
    if (st != ZX_OK) {
      return AE_ERROR;
    }

    ctx->pio_resources[ctx->pio_resource_i].base_address = io.minimum;
    ctx->pio_resources[ctx->pio_resource_i].alignment = io.alignment;
    ctx->pio_resources[ctx->pio_resource_i].address_length = io.address_length;

    ctx->pio_resource_i += 1;

  } else if (resource_is_irq(res)) {
    resource_irq_t irq;
    zx_status_t st = resource_parse_irq(res, &irq);
    if (st != ZX_OK) {
      return AE_ERROR;
    }
    for (size_t i = 0; i < irq.pin_count; i++) {
      ctx->irqs[ctx->irq_i].trigger = irq.trigger;
      ctx->irqs[ctx->irq_i].polarity = irq.polarity;
      ctx->irqs[ctx->irq_i].sharable = irq.sharable;
      ctx->irqs[ctx->irq_i].wake_capable = irq.wake_capable;
      ctx->irqs[ctx->irq_i].pin = irq.pins[i];

      ctx->irq_i += 1;
    }
  }

  return AE_OK;
}

static ACPI_STATUS report_current_resources_count_cb(ACPI_RESOURCE* res, void* _ctx) {
  acpi_crs_ctx_t* ctx = (acpi_crs_ctx_t*)_ctx;

  if (resource_is_memory(res)) {
    resource_memory_t mem;
    zx_status_t st = resource_parse_memory(res, &mem);
    if ((st != ZX_OK) || (mem.minimum != mem.maximum)) {
      return AE_ERROR;
    }
    ctx->mmio_resource_count += 1;

  } else if (resource_is_address(res)) {
    resource_address_t addr;
    zx_status_t st = resource_parse_address(res, &addr);
    if (st != ZX_OK) {
      return AE_ERROR;
    }
    if ((addr.resource_type == RESOURCE_ADDRESS_MEMORY) && addr.min_address_fixed &&
        addr.max_address_fixed && (addr.maximum < addr.minimum)) {
      ctx->mmio_resource_count += 1;
    }

  } else if (resource_is_io(res)) {
    resource_io_t io;
    zx_status_t st = resource_parse_io(res, &io);
    if (st != ZX_OK) {
      return AE_ERROR;
    }
    ctx->pio_resource_count += 1;

  } else if (resource_is_irq(res)) {
    ctx->irq_count += res->Data.Irq.InterruptCount;
  }

  return AE_OK;
}

static zx_status_t report_current_resources(acpi_device_t* dev) {
  acpi_crs_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  if (dev->got_resources) {
    return ZX_OK;
  }

  // call _CRS to count number of resources
  ACPI_STATUS acpi_status =
      AcpiWalkResources(dev->ns_node, (char*)"_CRS", report_current_resources_count_cb, &ctx);
  if ((acpi_status != AE_NOT_FOUND) && (acpi_status != AE_OK)) {
    return acpi_to_zx_status(acpi_status);
  }

  if (ctx.pio_resource_count == 0 && ctx.mmio_resource_count == 0 && ctx.irq_count == 0) {
    return ZX_OK;
  }

  // allocate resources
  ctx.pio_resources = calloc(ctx.pio_resource_count, sizeof(acpi_device_pio_resource_t));
  if (!ctx.pio_resources) {
    return ZX_ERR_NO_MEMORY;
  }
  ctx.mmio_resources = calloc(ctx.mmio_resource_count, sizeof(acpi_device_mmio_resource_t));
  if (!ctx.mmio_resources) {
    free(ctx.pio_resources);
    return ZX_ERR_NO_MEMORY;
  }
  ctx.irqs = calloc(ctx.irq_count, sizeof(acpi_device_irq_t));
  if (!ctx.irqs) {
    free(ctx.pio_resources);
    free(ctx.mmio_resources);
    return ZX_ERR_NO_MEMORY;
  }

  // call _CRS again and fill in resources
  acpi_status =
      AcpiWalkResources(dev->ns_node, (char*)"_CRS", report_current_resources_resource_cb, &ctx);
  if ((acpi_status != AE_NOT_FOUND) && (acpi_status != AE_OK)) {
    free(ctx.pio_resources);
    free(ctx.mmio_resources);
    free(ctx.irqs);
    return acpi_to_zx_status(acpi_status);
  }

  dev->pio_resources = ctx.pio_resources;
  dev->pio_resource_count = ctx.pio_resource_count;
  dev->mmio_resources = ctx.mmio_resources;
  dev->mmio_resource_count = ctx.mmio_resource_count;
  dev->irqs = ctx.irqs;
  dev->irq_count = ctx.irq_count;

  zxlogf(TRACE, "acpi-bus[%s]: found %zd port resources %zd memory resources %zx irqs\n",
         device_get_name(dev->zxdev), dev->pio_resource_count, dev->mmio_resource_count,
         dev->irq_count);
  if (driver_get_log_flags() & DDK_LOG_SPEW) {
    zxlogf(SPEW, "port resources:\n");
    for (size_t i = 0; i < dev->pio_resource_count; i++) {
      zxlogf(SPEW, "  %02zd: addr=0x%x length=0x%x align=0x%x\n", i,
             dev->pio_resources[i].base_address, dev->pio_resources[i].address_length,
             dev->pio_resources[i].alignment);
    }
    zxlogf(SPEW, "memory resources:\n");
    for (size_t i = 0; i < dev->mmio_resource_count; i++) {
      zxlogf(SPEW, "  %02zd: addr=0x%x length=0x%x align=0x%x writeable=%d\n", i,
             dev->mmio_resources[i].base_address, dev->mmio_resources[i].address_length,
             dev->mmio_resources[i].alignment, dev->mmio_resources[i].writeable);
    }
    zxlogf(SPEW, "irqs:\n");
    for (size_t i = 0; i < dev->irq_count; i++) {
      const char* trigger;
      switch (dev->irqs[i].trigger) {
        case ACPI_IRQ_TRIGGER_EDGE:
          trigger = "edge";
          break;
        case ACPI_IRQ_TRIGGER_LEVEL:
          trigger = "level";
          break;
        default:
          trigger = "bad_trigger";
          break;
      }
      const char* polarity;
      switch (dev->irqs[i].polarity) {
        case ACPI_IRQ_ACTIVE_BOTH:
          polarity = "both";
          break;
        case ACPI_IRQ_ACTIVE_LOW:
          polarity = "low";
          break;
        case ACPI_IRQ_ACTIVE_HIGH:
          polarity = "high";
          break;
        default:
          polarity = "bad_polarity";
          break;
      }
      zxlogf(SPEW, "  %02zd: pin=%u %s %s %s %s\n", i, dev->irqs[i].pin, trigger, polarity,
             (dev->irqs[i].sharable == ACPI_IRQ_SHARED) ? "shared" : "exclusive",
             dev->irqs[i].wake_capable ? "wake" : "nowake");
    }
  }

  dev->got_resources = true;

  return ZX_OK;
}

static zx_status_t acpi_op_get_pio(void* ctx, uint32_t index, zx_handle_t* out_pio) {
  acpi_device_t* dev = (acpi_device_t*)ctx;
  mtx_lock(&dev->lock);

  zx_status_t st = report_current_resources(dev);
  if (st != ZX_OK) {
    goto unlock;
  }

  if (index >= dev->pio_resource_count) {
    st = ZX_ERR_NOT_FOUND;
    goto unlock;
  }

  acpi_device_pio_resource_t* res = dev->pio_resources + index;

  zx_handle_t resource;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  // TODO: figure out what to pass to name here
  st = zx_resource_create(get_root_resource(), ZX_RSRC_KIND_IOPORT, res->base_address,
                          res->address_length, device_get_name(dev->zxdev), 0, &resource);
  if (st != ZX_OK) {
    goto unlock;
  }

  *out_pio = resource;

unlock:
  mtx_unlock(&dev->lock);
  return st;
}

static zx_status_t acpi_op_get_mmio(void* ctx, uint32_t index, acpi_mmio_t* out_mmio) {
  acpi_device_t* dev = (acpi_device_t*)ctx;
  mtx_lock(&dev->lock);

  zx_status_t st = report_current_resources(dev);
  if (st != ZX_OK) {
    goto unlock;
  }

  if (index >= dev->mmio_resource_count) {
    st = ZX_ERR_NOT_FOUND;
    goto unlock;
  }

  acpi_device_mmio_resource_t* res = dev->mmio_resources + index;
  if (((res->base_address & (PAGE_SIZE - 1)) != 0) ||
      ((res->address_length & (PAGE_SIZE - 1)) != 0)) {
    zxlogf(ERROR, "acpi-bus[%s]: memory id=%d addr=0x%08x len=0x%x is not page aligned\n",
           device_get_name(dev->zxdev), index, res->base_address, res->address_length);
    st = ZX_ERR_NOT_FOUND;
    goto unlock;
  }

  zx_handle_t vmo;
  size_t size = res->address_length;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  st = zx_vmo_create_physical(get_root_resource(), res->base_address, size, &vmo);
  if (st != ZX_OK) {
    goto unlock;
  }

  out_mmio->offset = 0;
  out_mmio->size = size;
  out_mmio->vmo = vmo;

unlock:
  mtx_unlock(&dev->lock);
  return st;
}

static zx_status_t acpi_op_map_interrupt(void* ctx, int64_t which_irq, zx_handle_t* out_handle) {
  acpi_device_t* dev = (acpi_device_t*)ctx;
  mtx_lock(&dev->lock);

  zx_status_t st = report_current_resources(dev);
  if (st != ZX_OK) {
    goto unlock;
  }

  if ((uint)which_irq >= dev->irq_count) {
    st = ZX_ERR_NOT_FOUND;
    goto unlock;
  }

  acpi_device_irq_t* irq = dev->irqs + which_irq;
  uint32_t mode = ZX_INTERRUPT_MODE_DEFAULT;
  st = ZX_OK;
  switch (irq->trigger) {
    case ACPI_IRQ_TRIGGER_EDGE:
      switch (irq->polarity) {
        case ACPI_IRQ_ACTIVE_BOTH:
          mode = ZX_INTERRUPT_MODE_EDGE_BOTH;
          break;
        case ACPI_IRQ_ACTIVE_LOW:
          mode = ZX_INTERRUPT_MODE_EDGE_LOW;
          break;
        case ACPI_IRQ_ACTIVE_HIGH:
          mode = ZX_INTERRUPT_MODE_EDGE_HIGH;
          break;
        default:
          st = ZX_ERR_INVALID_ARGS;
          break;
      }
      break;
    case ACPI_IRQ_TRIGGER_LEVEL:
      switch (irq->polarity) {
        case ACPI_IRQ_ACTIVE_LOW:
          mode = ZX_INTERRUPT_MODE_LEVEL_LOW;
          break;
        case ACPI_IRQ_ACTIVE_HIGH:
          mode = ZX_INTERRUPT_MODE_LEVEL_HIGH;
          break;
        default:
          st = ZX_ERR_INVALID_ARGS;
          break;
      }
      break;
    default:
      st = ZX_ERR_INVALID_ARGS;
      break;
  }
  if (st != ZX_OK) {
    goto unlock;
  }
  zx_handle_t handle;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  st = zx_interrupt_create(get_root_resource(), irq->pin, ZX_INTERRUPT_REMAP_IRQ | mode, &handle);
  if (st != ZX_OK) {
    goto unlock;
  }
  *out_handle = handle;

unlock:
  mtx_unlock(&dev->lock);
  return st;
}

static zx_status_t acpi_op_get_bti(void* ctx, uint32_t bdf, uint32_t index, zx_handle_t* bti) {
  // The x86 IOMMU world uses PCI BDFs as the hardware identifiers, so there
  // will only be one BTI per device.
  ZX_ASSERT(index == 0);
  // For dummy IOMMUs, the bti_id just needs to be unique.  For Intel IOMMUs,
  // the bti_ids correspond to PCI BDFs.
  zx_handle_t iommu_handle;
  zx_status_t status = iommu_manager_iommu_for_bdf(bdf, &iommu_handle);
  if (status != ZX_OK) {
    return status;
  }
  return zx_bti_create(iommu_handle, 0, bdf, bti);
}

static zx_status_t acpi_op_connect_sysmem(void* ctx, zx_handle_t handle) {
  acpi_device_t* dev = (acpi_device_t*)ctx;
  mtx_lock(&dev->lock);

  sysmem_protocol_t sysmem;
  zx_status_t st = device_get_protocol(dev->platform_bus, ZX_PROTOCOL_SYSMEM, &sysmem);
  if (st != ZX_OK) {
    zx_handle_close(handle);
    goto unlock;
  }
  st = sysmem_connect(&sysmem, handle);
unlock:
  mtx_unlock(&dev->lock);
  return st;
}

static zx_status_t acpi_op_register_sysmem_heap(void* ctx, uint64_t heap, zx_handle_t handle) {
  acpi_device_t* dev = (acpi_device_t*)ctx;
  mtx_lock(&dev->lock);

  sysmem_protocol_t sysmem;
  zx_status_t st = device_get_protocol(dev->platform_bus, ZX_PROTOCOL_SYSMEM, &sysmem);
  if (st != ZX_OK) {
    zx_handle_close(handle);
    goto unlock;
  }

  st = sysmem_register_heap(&sysmem, heap, handle);
unlock:
  mtx_unlock(&dev->lock);
  return st;
}

// TODO marking unused until we publish some devices
static __attribute__((unused)) acpi_protocol_ops_t acpi_proto = {
    .get_pio = acpi_op_get_pio,
    .get_mmio = acpi_op_get_mmio,
    .map_interrupt = acpi_op_map_interrupt,
    .get_bti = acpi_op_get_bti,
    .connect_sysmem = acpi_op_connect_sysmem,
    .register_sysmem_heap = acpi_op_register_sysmem_heap,
};

static const char* hid_from_acpi_devinfo(ACPI_DEVICE_INFO* info) {
  const char* hid = NULL;
  if ((info->Valid & ACPI_VALID_HID) && (info->HardwareId.Length > 0) &&
      ((info->HardwareId.Length - 1) <= sizeof(uint64_t))) {
    hid = (const char*)info->HardwareId.String;
  }
  return hid;
}

zx_device_t* publish_device(zx_device_t* parent, zx_device_t* platform_bus, ACPI_HANDLE handle,
                            ACPI_DEVICE_INFO* info, const char* name, uint32_t protocol_id,
                            void* protocol_ops) {
  zx_device_prop_t props[4];
  int propcount = 0;

  char acpi_name[5] = {0};
  if (!name) {
    memcpy(acpi_name, &info->Name, sizeof(acpi_name) - 1);
    name = (const char*)acpi_name;
  }

  // Publish HID in device props
  const char* hid = hid_from_acpi_devinfo(info);
  if (hid) {
    props[propcount].id = BIND_ACPI_HID_0_3;
    props[propcount++].value = htobe32(*((uint32_t*)(hid)));
    props[propcount].id = BIND_ACPI_HID_4_7;
    props[propcount++].value = htobe32(*((uint32_t*)(hid + 4)));
  }

  // Publish the first CID in device props
  const char* cid = (const char*)info->CompatibleIdList.Ids[0].String;
  if ((info->Valid & ACPI_VALID_CID) && (info->CompatibleIdList.Count > 0) &&
      ((info->CompatibleIdList.Ids[0].Length - 1) <= sizeof(uint64_t))) {
    props[propcount].id = BIND_ACPI_CID_0_3;
    props[propcount++].value = htobe32(*((uint32_t*)(cid)));
    props[propcount].id = BIND_ACPI_CID_4_7;
    props[propcount++].value = htobe32(*((uint32_t*)(cid + 4)));
  }

  if (driver_get_log_flags() & DDK_LOG_SPEW) {
    // ACPI names are always 4 characters in a uint32
    zxlogf(SPEW, "acpi: got device %s\n", acpi_name);
    if (info->Valid & ACPI_VALID_HID) {
      zxlogf(SPEW, "     HID=%s\n", info->HardwareId.String);
    } else {
      zxlogf(SPEW, "     HID=invalid\n");
    }
    if (info->Valid & ACPI_VALID_ADR) {
      zxlogf(SPEW, "     ADR=0x%" PRIx64 "\n", (uint64_t)info->Address);
    } else {
      zxlogf(SPEW, "     ADR=invalid\n");
    }
    if (info->Valid & ACPI_VALID_CID) {
      zxlogf(SPEW, "    CIDS=%d\n", info->CompatibleIdList.Count);
      for (uint i = 0; i < info->CompatibleIdList.Count; i++) {
        zxlogf(SPEW, "     [%u] %s\n", i, info->CompatibleIdList.Ids[i].String);
      }
    } else {
      zxlogf(SPEW, "     CID=invalid\n");
    }
    zxlogf(SPEW, "    devprops:\n");
    for (int i = 0; i < propcount; i++) {
      zxlogf(SPEW, "     [%d] id=0x%08x value=0x%08x\n", i, props[i].id, props[i].value);
    }
  }

  acpi_device_t* dev = calloc(1, sizeof(acpi_device_t));
  if (!dev) {
    return NULL;
  }
  dev->platform_bus = platform_bus;

  dev->ns_node = handle;

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = name,
      .ctx = dev,
      .ops = &acpi_device_proto,
      .proto_id = protocol_id,
      .proto_ops = protocol_ops,
      .props = (propcount > 0) ? props : NULL,
      .prop_count = propcount,
  };

  zx_status_t status;
  if ((status = device_add(parent, &args, &dev->zxdev)) != ZX_OK) {
    zxlogf(ERROR, "acpi: error %d in device_add, parent=%s(%p)\n", status, device_get_name(parent),
           parent);
    free(dev);
    return NULL;
  } else {
    zxlogf(ERROR, "acpi: published device %s(%p), parent=%s(%p), handle=%p\n", name, dev,
           device_get_name(parent), parent, (void*)dev->ns_node);
    return dev->zxdev;
  }
}

static void acpi_apply_workarounds(ACPI_HANDLE object, ACPI_DEVICE_INFO* info) {
  ACPI_STATUS acpi_status;
  // Slate workaround: Turn on the HID controller.
  if (!memcmp(&info->Name, "I2C0", 4)) {
    ACPI_BUFFER buffer = {
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };
    acpi_status = AcpiEvaluateObject(object, (char*)"H00A._PR0", NULL, &buffer);
    if (acpi_status == AE_OK) {
      ACPI_OBJECT* pkg = buffer.Pointer;
      for (unsigned i = 0; i < pkg->Package.Count; i++) {
        ACPI_OBJECT* ref = &pkg->Package.Elements[i];
        if (ref->Type != ACPI_TYPE_LOCAL_REFERENCE) {
          zxlogf(TRACE, "acpi: Ignoring wrong type 0x%x\n", ref->Type);
        } else {
          zxlogf(TRACE, "acpi: Enabling HID controller at I2C0.H00A._PR0[%u]\n", i);
          acpi_status = AcpiEvaluateObject(ref->Reference.Handle, (char*)"_ON", NULL, NULL);
          if (acpi_status != AE_OK) {
            zxlogf(ERROR, "acpi: acpi error 0x%x in I2C0._PR0._ON\n", acpi_status);
          }
        }
      }
      AcpiOsFree(buffer.Pointer);
    }
  }
  // Acer workaround: Turn on the HID controller.
  else if (!memcmp(&info->Name, "I2C1", 4)) {
    zxlogf(TRACE, "acpi: Enabling HID controller at I2C1\n");
    acpi_status = AcpiEvaluateObject(object, (char*)"_PS0", NULL, NULL);
    if (acpi_status != AE_OK) {
      zxlogf(ERROR, "acpi: acpi error in I2C1._PS0: 0x%x\n", acpi_status);
    }
  }
}

static ACPI_STATUS acpi_ns_walk_callback(ACPI_HANDLE object, uint32_t nesting_level, void* context,
                                         void** status) {
  ACPI_DEVICE_INFO* info = NULL;
  ACPI_STATUS acpi_status = AcpiGetObjectInfo(object, &info);
  if (acpi_status != AE_OK) {
    return acpi_status;
  }

  publish_acpi_device_ctx_t* ctx = (publish_acpi_device_ctx_t*)context;
  zx_device_t* acpi_root = ctx->acpi_root;
  zx_device_t* sys_root = ctx->sys_root;
  zx_device_t* platform_bus = ctx->platform_bus;

  acpi_apply_workarounds(object, info);
  if (!memcmp(&info->Name, "HDAS", 4)) {
    // We must have already seen at least one PCI root due to traversal order.
    if (ctx->last_pci == 0xFF) {
      zxlogf(ERROR, "acpi: Found HDAS node, but no prior PCI root was discovered!\n");
    } else if (!(info->Valid & ACPI_VALID_ADR)) {
      zxlogf(ERROR, "acpi: no valid ADR found for HDA device\n");
    } else {
      // Attaching metadata to the HDAS device /dev/sys/pci/...
      zx_status_t status =
          nhlt_publish_metadata(sys_root, ctx->last_pci, (uint64_t)info->Address, object);
      if ((status != ZX_OK) && (status != ZX_ERR_NOT_FOUND)) {
        zxlogf(ERROR, "acpi: failed to publish NHLT metadata\n");
      }
    }
  }

  const char* hid = hid_from_acpi_devinfo(info);
  if (hid == 0) {
    goto out;
  }
  const char* cid = NULL;
  if ((info->Valid & ACPI_VALID_CID) && (info->CompatibleIdList.Count > 0) &&
      // IDs may be 7 or 8 bytes, and Length includes the null byte
      (info->CompatibleIdList.Ids[0].Length == HID_LENGTH ||
       info->CompatibleIdList.Ids[0].Length == HID_LENGTH + 1)) {
    cid = (const char*)info->CompatibleIdList.Ids[0].String;
  }

  if ((!memcmp(hid, PCI_EXPRESS_ROOT_HID_STRING, HID_LENGTH) ||
       !memcmp(hid, PCI_ROOT_HID_STRING, HID_LENGTH))) {
    pci_init(sys_root, object, info, ctx);
  } else if (!memcmp(hid, BATTERY_HID_STRING, HID_LENGTH)) {
    battery_init(acpi_root, object);
  } else if (!memcmp(hid, LID_HID_STRING, HID_LENGTH)) {
    lid_init(acpi_root, object);
  } else if (!memcmp(hid, PWRSRC_HID_STRING, HID_LENGTH)) {
    pwrsrc_init(acpi_root, object);
  } else if (!memcmp(hid, EC_HID_STRING, HID_LENGTH)) {
    ec_init(acpi_root, object);
  } else if (!memcmp(hid, GOOGLE_TBMC_HID_STRING, HID_LENGTH)) {
    tbmc_init(acpi_root, object);
  } else if (!memcmp(hid, GOOGLE_CROS_EC_HID_STRING, HID_LENGTH)) {
    cros_ec_lpc_init(acpi_root, object);
  } else if (!memcmp(hid, DPTF_THERMAL_HID_STRING, HID_LENGTH)) {
    thermal_init(acpi_root, info, object);
  } else if (!memcmp(hid, I8042_HID_STRING, HID_LENGTH) ||
             (cid && !memcmp(cid, I8042_HID_STRING, HID_LENGTH))) {
    publish_device(acpi_root, platform_bus, object, info, "i8042", ZX_PROTOCOL_ACPI, &acpi_proto);
  } else if (!memcmp(hid, RTC_HID_STRING, HID_LENGTH) ||
             (cid && !memcmp(cid, RTC_HID_STRING, HID_LENGTH))) {
    publish_device(acpi_root, platform_bus, object, info, "rtc", ZX_PROTOCOL_ACPI, &acpi_proto);
  } else if (!memcmp(hid, GOLDFISH_PIPE_HID_STRING, HID_LENGTH)) {
    publish_device(acpi_root, platform_bus, object, info, "goldfish", ZX_PROTOCOL_ACPI,
                   &acpi_proto);
  } else if (!memcmp(hid, SERIAL_HID_STRING, HID_LENGTH)) {
    publish_device(acpi_root, platform_bus, object, info, "serial", ZX_PROTOCOL_ACPI, &acpi_proto);
  }

out:
  ACPI_FREE(info);

  return AE_OK;
}

zx_status_t acpi_suspend(uint32_t flags) {
  switch (flags & DEVICE_SUSPEND_REASON_MASK) {
    case DEVICE_SUSPEND_FLAG_MEXEC: {
      AcpiTerminate();
      return ZX_OK;
    }
    case DEVICE_SUSPEND_FLAG_REBOOT:
      if (flags == DEVICE_SUSPEND_FLAG_REBOOT_BOOTLOADER) {
        reboot_bootloader();
      } else if (flags == DEVICE_SUSPEND_FLAG_REBOOT_RECOVERY) {
        reboot_recovery();
      } else {
        reboot();
      }
      // Kill this driver so that the IPC channel gets closed; devmgr will
      // perform a fallback that should shutdown or reboot the machine.
      exit(0);
    case DEVICE_SUSPEND_FLAG_POWEROFF:
      poweroff();
      exit(0);
    case DEVICE_SUSPEND_FLAG_SUSPEND_RAM:
      return suspend_to_ram();
    default:
      return ZX_ERR_NOT_SUPPORTED;
  };
}

zx_status_t publish_acpi_devices(zx_device_t* parent, zx_device_t* sys_root,
                                 zx_device_t* acpi_root) {
  zx_status_t status = pwrbtn_init(acpi_root);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi: failed to initialize pwrbtn device: %d\n", status);
  }

  // Walk the ACPI namespace for devices and publish them
  // Only publish a single PCI device
  publish_acpi_device_ctx_t ctx = {
      .acpi_root = acpi_root,
      .sys_root = sys_root,
      .platform_bus = parent,
      .found_pci = false,
      .last_pci = 0xFF,
  };
  ACPI_STATUS acpi_status =
      AcpiWalkNamespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, MAX_NAMESPACE_DEPTH,
                        acpi_ns_walk_callback, NULL, &ctx, NULL);
  if (acpi_status != AE_OK) {
    return ZX_ERR_BAD_STATE;
  } else {
    return ZX_OK;
  }
}
