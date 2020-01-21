// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hog.h"

#include <lib/device-protocol/bt-gatt-svc.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include "boot_descriptors.h"

#define BT_HOG_STATUS_OK(s) \
  (s->status == ZX_OK && s->att_ecode == BT_GATT_ERR_NO_ERROR)

// These are redefined here so it's easy to change them while developing.
// Consider replacing trace calls with tracing library instead of using DDK's
// printf.

static zx_status_t hogd_hid_query(void* ctx, uint32_t options,
                                  hid_info_t* info) {
  zxlogf(TRACE, "bt_hog hog_hid_query, ctx: %p, options: %i\n", ctx, options);
  hogd_device_t* child = (hogd_device_t*)ctx;
  switch (child->device_type) {
    case HOGD_DEVICE_BOOT_KEYBOARD:
      info->dev_num = HID_DEVICE_CLASS_KBD;
      info->device_class = HID_DEVICE_CLASS_KBD;
      info->boot_device = true;
      break;
    case HOGD_DEVICE_BOOT_MOUSE:
      info->dev_num = HID_DEVICE_CLASS_POINTER;
      info->device_class = HID_DEVICE_CLASS_POINTER;
      info->boot_device = true;
      break;
    default:
      info->dev_num = HID_DEVICE_CLASS_OTHER;
      info->device_class = HID_DEVICE_CLASS_OTHER;
      info->boot_device = false;
  }
  return ZX_OK;
}

static zx_status_t hogd_hid_start(void* ctx, const hidbus_ifc_protocol_t* ifc) {
  zxlogf(TRACE, "bt_hog hog_hid_start, ctx: %p, cookie: %p\n", ctx, ifc->ctx);
  hogd_device_t* child = (hogd_device_t*)ctx;
  mtx_lock(&child->lock);
  if (child->ifc.ops) {
    mtx_unlock(&child->lock);
    return ZX_ERR_ALREADY_BOUND;
  }
  child->ifc = *ifc;
  mtx_unlock(&child->lock);

  return ZX_OK;
}

static void hogd_hid_stop(void* ctx) {
  // TODO(zbowling): Implement stop. It's not entirely clear what we should do.
  zxlogf(TRACE, "bt_hog hogd_hid_stop, ctx: %p\n", ctx);
}

static zx_status_t hogd_hid_get_descriptor(void* ctx, hid_description_type_t desc_type,
                                           void* out_data_buffer, size_t data_size,
                                           size_t* out_data_actual) {
  hogd_device_t* hogd_child = (hogd_device_t*)ctx;
  zxlogf(TRACE, "bt_hog hogd_hid_get_descriptor, ctx: %p, desc_type: %u\n", ctx,
         desc_type);
  if (desc_type != HID_DESCRIPTION_TYPE_REPORT) {
    return ZX_ERR_NOT_FOUND;
  }

  // HACK: Boot devices technically shouldn't have descriptors or at least the
  // information in them shouldn't be used to influence how you parse boot mode
  // data. Return fake descriptors to HID with basic keyboard/mouse devices to
  // to make HID happy and to work around bugs up and down the stack related to
  // that assumption.
  switch (hogd_child->device_type) {
    case HOGD_DEVICE_BOOT_KEYBOARD:
      if (data_size < sizeof(kbd_hid_report_desc)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      memcpy(out_data_buffer, kbd_hid_report_desc, sizeof(kbd_hid_report_desc));
      *out_data_actual = sizeof(kbd_hid_report_desc);
      break;
    case HOGD_DEVICE_BOOT_MOUSE:
      if (data_size < sizeof(mouse_hid_report_desc)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      memcpy(out_data_buffer, mouse_hid_report_desc, sizeof(mouse_hid_report_desc));
      *out_data_actual = sizeof(mouse_hid_report_desc);
      break;
    default:
      if (data_size <hogd_child->parent->hid_descriptor_len) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      memcpy(out_data_buffer, hogd_child->parent->hid_descriptor,
             hogd_child->parent->hid_descriptor_len);
      *out_data_actual = hogd_child->parent->hid_descriptor_len;
  }

  return ZX_OK;
}

static zx_status_t hogd_hid_get_report(void* ctx, uint8_t rpt_type,
                                       uint8_t rpt_id, void* data, size_t len,
                                       size_t* out_len) {
  zxlogf(TRACE,
         "bt_hog hogd_hid_get_report, ctx: %p, rpt_type: %u, rpt_id: %u\n", ctx,
         rpt_type, rpt_id);
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t hogd_hid_set_report(void* ctx, uint8_t rpt_type,
                                       uint8_t rpt_id, const void* data,
                                       size_t len) {
  zxlogf(TRACE,
         "bt_hog hogd_hid_set_report, ctx: %p, rpt_type: %u, rpt_id: %u\n", ctx,
         rpt_type, rpt_id);
  hogd_device_t* child = (hogd_device_t*)ctx;
  // We are explicitly doing this as a write without response message because
  // this is a synchronous HIDBUS API and we don't want to block it to wait to
  // know if the caps lock/num lock light got turned on properly or not as
  // state is stored host side anyways.
  if (child->has_output_report_id) {
    bt_gatt_svc_write_characteristic(&child->parent->gatt_svc,
                                     child->output_report_id, data, len, NULL,
                                     child);
  }
  return ZX_OK;
}

static zx_status_t hogd_hid_get_idle(void* ctx, uint8_t rpt_id,
                                     uint8_t* duration) {
  zxlogf(TRACE, "bt_hog hogd_hid_get_idle, ctx: %p, rpt_id: %u\n", ctx, rpt_id);
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t hogd_hid_set_idle(void* ctx, uint8_t rpt_id,
                                     uint8_t duration) {
  zxlogf(TRACE, "bt_hog hogd_hid_set_idle, ctx: %p, rpt_id: %u, duration: %u\n",
         ctx, rpt_id, duration);
  // TODO(zbowling): wire into org.bluetooth.characteristic.hid_control_point
  return ZX_OK;
}

static zx_status_t hogd_hid_get_protocol(void* ctx, uint8_t* protocol) {
  zxlogf(TRACE, "bt_hog hogd_hid_get_protocol, ctx: %p\n", ctx);
  // TODO(zbowling): Support report mode.
  // Querying the actual protocol mode on the device is async on BT and hidbus
  // is sync.
  *protocol = 0;
  return ZX_OK;
}

static zx_status_t hogd_hid_set_protocol(void* ctx, uint8_t protocol) {
  zxlogf(TRACE, "bt_hog hogd_hid_set_protocol, ctx: %p, protocol: %u\n", ctx,
         protocol);
  // We are explicitly setting BOOT protocol internally so ignore attempts to
  // change it until report mode is fully implemented.
  return ZX_OK;
}

static void hogd_release(void* ctx) {
  zxlogf(TRACE, "bt_hog hogd_release, ctx: %p\n", ctx);
  hogd_t* hogd = (hogd_t*)ctx;
  if (hogd->hid_descriptor)
    free(hogd->hid_descriptor);

  free(hogd);
}

static void hogd_unbind(void* ctx) {
  zxlogf(TRACE, "bt_hog hogd_unbind, ctx: %p\n", ctx);
  hogd_t* hogd = (hogd_t*)ctx;
  // We are unbinding so we should stop receiving notifications on this device.
  bt_gatt_svc_stop(&hogd->gatt_svc);
}

static void hogd_child_release(void* ctx) {
  zxlogf(TRACE, "bt_hog hogd_child_release, ctx: %p\n", ctx);
  hogd_device_t* child = (hogd_device_t*)ctx;
  child->is_initialized = false;
}

static void hogd_child_unbind(void* ctx) {
  zxlogf(TRACE, "bt_hog hogd_child_unbind, ctx: %p\n", ctx);
  hogd_device_t* child = (hogd_device_t*)ctx;
  child->is_initialized = false;
  // TODO(zbowling): Explicitly remove any notifications we are still receiving.
}

static hidbus_protocol_ops_t hogd_hidbus_ops = {
    .query = hogd_hid_query,
    .start = hogd_hid_start,
    .stop = hogd_hid_stop,
    .get_descriptor = hogd_hid_get_descriptor,
    .get_report = hogd_hid_get_report,
    .set_report = hogd_hid_set_report,
    .get_idle = hogd_hid_get_idle,
    .set_idle = hogd_hid_set_idle,
    .get_protocol = hogd_hid_get_protocol,
    .set_protocol = hogd_hid_set_protocol,
};

static zx_protocol_device_t hogd_dev_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = hogd_unbind,
    .release = hogd_release,
};

static zx_protocol_device_t hogd_child_dev_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = hogd_child_unbind,
    .release = hogd_child_release,
};

// Catch-all handler for status callbacks we can't handle explicitly.
static void hogd_noop_status(void* ctx, const bt_gatt_status_t* status,
                             bt_gatt_id_t id) {
  zxlogf(TRACE, "bt_hog hogd_noop_status, ctx: %p\n", ctx);
  if (!BT_HOG_STATUS_OK(status)) {
    zxlogf(ERROR, "bt_hog status, att_ecode: %u, id: %lu\n", status->att_ecode,
           id);
  }
}

static inline void hogd_log_blob(const char* name, const uint8_t* value,
                                 size_t len) {
  if (zxlog_level_enabled(SPEW)) {
    zxlogf(SPEW, "%s: ", name);
    for (size_t i = 0; i < len; i++) {
      zxlogf(SPEW, "%x ", value[i]);
    }
    zxlogf(SPEW, "\n");
  }
}

static void hogd_report_notification(void* ctx, bt_gatt_id_t id,
                                     const void* value, size_t len) {
  zxlogf(TRACE, "bt_hog hogd_report_notification, ctx: %p, id: %lu\n", ctx, id);
  hogd_log_blob("bt_hog input event", value, len);
  hogd_device_t* child = (hogd_device_t*)ctx;
  if (!child) {
    zxlogf(ERROR, "bt_hog received input event for an uninitialized device\n");
    return;
  }

  // HACK: Trim report for mice.
  if (child->device_type == HOGD_DEVICE_BOOT_MOUSE && len > 3)
    len = 3;

  mtx_lock(&child->lock);
  if (child->ifc.ops) {
    hidbus_ifc_io_queue(&child->ifc, value, len, zx_clock_get_monotonic());
  }
  mtx_unlock(&child->lock);
}

static zx_status_t hogd_initialize_boot_device(hogd_device_t* child,
                                               hogd_device_type_t type,
                                               hogd_t* parent) {
  ZX_DEBUG_ASSERT(type == HOGD_DEVICE_BOOT_KEYBOARD ||
                  type == HOGD_DEVICE_BOOT_MOUSE);
  child->device_type = type;
  child->parent = parent;

  const char* dev_name;
  switch (type) {
    case HOGD_DEVICE_BOOT_KEYBOARD:
      dev_name = "bt_hog_boot_keyboard";
      break;
    case HOGD_DEVICE_BOOT_MOUSE:
      dev_name = "bt_hog_boot_mouse";
      break;
    default:
      dev_name = "bt_hog_input";
  }

  zx_status_t status;
  device_add_args_t args = {.version = DEVICE_ADD_ARGS_VERSION,
                            .name = dev_name,
                            .ctx = child,
                            .ops = &hogd_child_dev_ops,
                            .proto_id = ZX_PROTOCOL_HIDBUS,
                            .proto_ops = &hogd_hidbus_ops};

  status = device_add(parent->bus_dev, &args, &child->dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt_hog failed to create child device, status: %i\n", status);
    return status;
  }

  if (child->has_input_report_id) {
    const bt_gatt_notification_value_t notification_cb = {
        hogd_report_notification, child};
    bt_gatt_svc_enable_notifications(&parent->gatt_svc, child->input_report_id,
                                     &notification_cb, hogd_noop_status, child);
  }

  child->is_initialized = true;

  return status;
}

static void hogd_on_read_report_map(void* ctx, const bt_gatt_status_t* status,
                                    bt_gatt_id_t id, const void* value,
                                    size_t len) {
  zxlogf(TRACE, "bt_hog hogd_on_read_report_map, ctx: %p, id: %lu\n", ctx, id);
  hogd_t* hogd = (hogd_t*)ctx;
  if (!BT_HOG_STATUS_OK(status) || value == NULL || len == 0) {
    zxlogf(ERROR,
           "bt_hog driver was unable to read the report_map attribute (ERROR: "
           "%i ATT_ECODE: %i MAP_LEN: %lu)\n",
           status->status, status->att_ecode, len);
    // Unrecoverable state. Remove the device and let dev manger clean us up.
    device_remove_deprecated(hogd->bus_dev);
    return;
  }

  hogd_log_blob("bt_hog report map", value, len);

  // Copy the HID descriptor from report_map characteristic.
  hogd->hid_descriptor = malloc(len);
  memcpy(hogd->hid_descriptor, value, len);
  hogd->hid_descriptor_len = len;

  zxlogf(INFO,
         "bt_hog NOTE: Explicitly setting protocol mode to BOOT as REPORT mode "
         "is not supported by HIDBUS yet.\n");

  // Set protocol mode to boot mode again, just in case the asking for the
  // report descriptor flipped it out of boot mode.
  uint8_t val = BT_HOG_PROTOCOL_MODE_BOOT_MODE;
  bt_gatt_svc_write_characteristic(&hogd->gatt_svc, hogd->protocol_mode_id,
                                   &val, 1, NULL, hogd);

  // TODO(zbowling): In the future, we should only really have one device
  // exposed to HIDBUS and HIDBUS should handle all report notifications on it's
  // own regardless of protocol.
  if (hogd->boot_keyboard_device.has_input_report_id) {
    hogd_initialize_boot_device(&hogd->boot_keyboard_device,
                                HOGD_DEVICE_BOOT_KEYBOARD, hogd);
  }

  if (hogd->boot_mouse_device.has_input_report_id) {
    hogd_initialize_boot_device(&hogd->boot_mouse_device,
                                HOGD_DEVICE_BOOT_MOUSE, hogd);
  }
}

static inline void hogd_debug_log_uuid(const uint8_t value[16],
                                       const char* name) {
  zxlogf(
      SPEW,
      "%s UUID "
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
      name, value[15], value[14], value[13], value[12], value[11], value[10],
      value[9], value[8], value[7], value[6], value[5], value[4], value[3],
      value[2], value[1], value[0]);
}

static void hogd_connect(void* ctx, const bt_gatt_status_t* status,
                         const bt_gatt_chr_t* characteristics, size_t len) {
  if (!BT_HOG_STATUS_OK(status)) {
    zxlogf(WARN,
           "bt_hog driver has failed to enumerate service characteristics "
           "(ERROR: %i ATT_ECODE: %i)\n",
           status->status, status->att_ecode);
    return;
  }

  hogd_t* hogd = (hogd_t*)ctx;
  bt_gatt_uuid_t rmuuid = bt_gatt_make_uuid16(BT_HOG_REPORT_MAP);
  bt_gatt_uuid_t ruuid = bt_gatt_make_uuid16(BT_HOG_REPORT);
  bt_gatt_uuid_t pmuuid = bt_gatt_make_uuid16(BT_HOG_PROTOCOL_MODE);
  bt_gatt_uuid_t kiruuid =
      bt_gatt_make_uuid16(BT_HOG_BOOT_KEYBOARD_INPUT_REPORT);
  bt_gatt_uuid_t koruuid =
      bt_gatt_make_uuid16(BT_HOG_BOOT_KEYBOARD_OUTPUT_REPORT);
  bt_gatt_uuid_t miruuid = bt_gatt_make_uuid16(BT_HOG_BOOT_MOUSE_INPUT_REPORT);

  hogd_debug_log_uuid(rmuuid.bytes, "rmuuid");
  hogd_debug_log_uuid(pmuuid.bytes, "pmuuid");

  for (size_t chi = 0; chi < len; chi++) {
    hogd_debug_log_uuid(characteristics[chi].type.bytes, "characteristic");
    // If get multiple of these, against spec, we will always assume the last
    // one.

    // Report map characteristic.
    if (!hogd->has_report_map &&
        bt_gatt_compare_uuid(&characteristics[chi].type, &rmuuid) == 0) {
      hogd->has_report_map = true;
      hogd->report_map_id = characteristics[chi].id;
      continue;
    }

    // Protocol mode characteristic.
    if (!hogd->has_protocol_mode &&
        bt_gatt_compare_uuid(&characteristics[chi].type, &pmuuid) == 0) {
      hogd->has_protocol_mode = true;
      hogd->protocol_mode_id = characteristics[chi].id;
      continue;
    }

    // Boot input keyboard characteristic.
    if (!hogd->boot_keyboard_device.has_input_report_id &&
        bt_gatt_compare_uuid(&characteristics[chi].type, &kiruuid) == 0) {
      hogd->boot_keyboard_device.has_input_report_id = true;
      hogd->boot_keyboard_device.input_report_id = characteristics[chi].id;
      zxlogf(SPEW, "bt_hog boot keyboard input report id %lu\n",
             characteristics[chi].id);
      continue;
    }

    // Boot output keyboard characteristic.
    if (!hogd->boot_keyboard_device.has_output_report_id &&
        bt_gatt_compare_uuid(&characteristics[chi].type, &koruuid) == 0) {
      hogd->boot_keyboard_device.has_output_report_id = true;
      hogd->boot_keyboard_device.output_report_id = characteristics[chi].id;
      continue;
    }

    // Boot mouse input characteristic.
    if (!hogd->boot_mouse_device.has_input_report_id &&
        bt_gatt_compare_uuid(&characteristics[chi].type, &miruuid) == 0) {
      hogd->boot_mouse_device.has_input_report_id = true;
      hogd->boot_mouse_device.input_report_id = characteristics[chi].id;
      continue;
    }

    // Report characteristic. In report mode both the input and out descriptors
    // are the same ID.
    if (bt_gatt_compare_uuid(&characteristics[chi].type, &ruuid) == 0) {
      zxlogf(SPEW, "bt_hog report characteristic - handler id: %lu\n",
             characteristics[chi].id);
      continue;
    }
  }

  if (hogd->has_report_map && hogd->has_protocol_mode) {
    // Set protocol mode to boot mode
    uint8_t val = BT_HOG_PROTOCOL_MODE_BOOT_MODE;
    bt_gatt_svc_write_characteristic(&hogd->gatt_svc, hogd->protocol_mode_id,
                                     &val, 1, NULL, hogd);
    bt_gatt_svc_read_long_characteristic(&hogd->gatt_svc, hogd->report_map_id,
                                         0, UINT16_MAX, hogd_on_read_report_map,
                                         hogd);
  } else {
    zxlogf(ERROR,
           "bt_hog HID service is missing mandatory service attributes\n");
    device_remove_deprecated(hogd->bus_dev);
  }
}

zx_status_t bt_hog_bind(void* ctx, zx_device_t* device) {
  hogd_t* hogd = calloc(1, sizeof(hogd_t));
  if (!hogd) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status =
      device_get_protocol(device, ZX_PROTOCOL_BT_GATT_SVC, &hogd->gatt_svc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt_hog driver has failed to get GATT service protocol\n");
    return status;
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "bt_hog",
      .ctx = hogd,
      .ops = &hogd_dev_ops,
      .flags = DEVICE_ADD_NON_BINDABLE,
  };

  status = device_add(device, &args, &hogd->bus_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR,
           "bt_hog driver has failed to create a child device status: %i\n",
           status);
    return status;
  }

  bt_gatt_svc_connect(&hogd->gatt_svc, hogd_connect, hogd);

  return status;
}
