// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-device.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb/bus.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/hardware/usb/device/c/fidl.h>
#include <utf_conversion/utf_conversion.h>
#include <zircon/hw/usb.h>

#include "usb-bus.h"

namespace usb_bus {

struct UsbRequestInternal {
    // callback to client driver
    usb_request_complete_t complete_cb;
    // for queueing at the usb-bus level
    list_node_t node;
};
#define USB_REQ_TO_DEV_INTERNAL(req, size) \
    ((UsbRequestInternal *)((uintptr_t)(req) + (size)))
#define DEV_INTERNAL_TO_USB_REQ(ctx, size) ((usb_request_t *)((uintptr_t)(ctx) - (size)))

// By default we create devices for the interfaces on the first configuration.
// This table allows us to specify a different configuration for certain devices
// based on their VID and PID.
//
// TODO(voydanoff) Find a better way of handling this. For example, we could query to see
// if any interfaces on the first configuration have drivers that can bind to them.
// If not, then we could try the other configurations automatically instead of having
// this hard coded list of VID/PID pairs
struct UsbConfigOverride {
    uint16_t vid;
    uint16_t pid;
    uint8_t configuration;
};

static const UsbConfigOverride config_overrides[] = {
    { 0x0bda, 0x8153, 2 },  // Realtek ethernet dongle has CDC interface on configuration 2
    { 0, 0, 0 },
};

// This thread is for calling the usb request completion callback for requests received from our
// client. We do this on a separate thread because it is unsafe to call out on our own completion
// callback, which is called on the main thread of the USB HCI driver.
int UsbDevice::CallbackThread() {
    bool done = false;

    while (!done) {
        list_node_t temp_list = LIST_INITIAL_VALUE(temp_list);

        // Wait for new usb requests to complete or for signal to exit this thread.
        sync_completion_wait(&callback_thread_completion_, ZX_TIME_INFINITE);
        sync_completion_reset(&callback_thread_completion_);

        {
            fbl::AutoLock lock(&callback_lock_);

            done = callback_thread_stop_;

            // Copy completed requests to a temp list so we can process them outside of our lock.
            list_move(&completed_reqs_, &temp_list);
        }

        // Call completion callbacks outside of the lock.
        usb_request_t* req;
        UsbRequestInternal* req_int;
        while ((req_int = list_remove_head_type(&temp_list, UsbRequestInternal, node))) {
            req = DEV_INTERNAL_TO_USB_REQ(req_int, parent_req_size_);
            usb_request_complete(req, req->response.status, req->response.actual,
                                 &req_int->complete_cb);
        }
    }

    return 0;
}

void UsbDevice::StartCallbackThread() {
    // TODO(voydanoff) Once we have a way of knowing when a driver has bound to us, move the thread
    // start there so we don't have to start a thread unless we know we will need it.
    thrd_create_with_name(&callback_thread_,
                                   [](void* arg) -> int {
                                       return static_cast<UsbDevice*>(arg)->CallbackThread();
                                   }, this, "usb-device-callback-thread");
}

void UsbDevice::StopCallbackThread() {
    {
        fbl::AutoLock lock(&callback_lock_);
        callback_thread_stop_ = true;
    }

    sync_completion_signal(&callback_thread_completion_);
    thrd_join(callback_thread_, nullptr);
}

// usb request completion for the requests passed down to the HCI driver
void UsbDevice::RequestComplete(usb_request_t* req) {
    fbl::AutoLock lock(&callback_lock_);

    // move original request to completed_reqs list so it can be completed on the callback_thread
    UsbRequestInternal* req_int = USB_REQ_TO_DEV_INTERNAL(req, parent_req_size_);
    list_add_tail(&completed_reqs_, &req_int->node);
    sync_completion_signal(&callback_thread_completion_);
}


void UsbDevice::SetHubInterface(const usb_hub_interface_t* hub_intf) {
    if (hub_intf) {
        hub_intf_ = hub_intf;
    } else {
        hub_intf_.clear();
    }
}

const usb_configuration_descriptor_t* UsbDevice::GetConfigDesc(uint8_t config) {
    for (auto& desc_array : config_descs_) {
        auto* desc = reinterpret_cast<usb_configuration_descriptor_t*>(desc_array.get());
        if (desc->bConfigurationValue == config) {
            return desc;
        }
    }
    return nullptr;
}

zx_status_t UsbDevice::DdkGetProtocol(uint32_t proto_id, void* protocol) {
    if (proto_id == ZX_PROTOCOL_USB) {
        auto* usb_proto = static_cast<usb_protocol_t*>(protocol);
        usb_proto->ctx = this;
        usb_proto->ops = &usb_protocol_ops_;
        return ZX_OK;
    } else {
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void UsbDevice::DdkUnbind() {
    DdkRemove();
}

void UsbDevice::DdkRelease() {
    StopCallbackThread();

    // Release the reference now that devmgr no longer has a pointer to this object.
    __UNUSED bool dummy = Release();
}

void UsbDevice::ControlComplete(void* ctx, usb_request_t* req) {
    sync_completion_signal(static_cast<sync_completion_t*>(ctx));
}

zx_status_t UsbDevice::Control(uint8_t request_type, uint8_t request, uint16_t value,
                               uint16_t index, zx_time_t timeout, const void* write_buffer,
                               size_t write_size, void* out_read_buffer, size_t read_size,
                               size_t* out_read_actual) {
    size_t length;
    bool out = ((request_type & USB_DIR_MASK) == USB_DIR_OUT);
    if (out) {
        length = write_size;
    } else {
        length = read_size;
    }
    if (length > UINT16_MAX) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    usb_request_t* req = NULL;
    bool use_free_list = (length == 0);
    if (use_free_list) {
        fbl::AutoLock lock(&free_reqs_lock_);
        req = usb_request_pool_get(&free_reqs_, length);
    }

    if (req == NULL) {
        auto status = usb_request_alloc(&req, length, 0, req_size_);
        if (status != ZX_OK) {
            return status;
        }
    }

    // fill in protocol data
    usb_setup_t* setup = &req->setup;
    setup->bmRequestType = request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = static_cast<uint16_t>(length);

    if (out) {
        if (length > 0 && write_buffer == NULL) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (length > write_size) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
    } else {
        if (length > 0 && out_read_buffer == NULL) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (length > read_size) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
    }

    if (length > 0 && out) {
        usb_request_copy_to(req, write_buffer, length, 0);
    }

    sync_completion_t completion;

    req->header.device_id = device_id_;
    req->header.length = length;
    // We call this directly instead of via hci_queue, as it's safe to call our
    // own completion callback, and prevents clients getting into odd deadlocks.
    usb_request_complete_t complete = {
        .callback = ControlComplete,
        .ctx = &completion,
    };
    hci_.RequestQueue(req, &complete);
    auto status = sync_completion_wait(&completion, timeout);

    if (status == ZX_OK) {
        status = req->response.status;
    } else if (status == ZX_ERR_TIMED_OUT) {
        // cancel transactions and wait for request to be completed
        sync_completion_reset(&completion);
        status = hci_.CancelAll(device_id_, 0);
        if (status == ZX_OK) {
            sync_completion_wait(&completion, ZX_TIME_INFINITE);
            status = ZX_ERR_TIMED_OUT;
        }
    }
    if (status == ZX_OK && !out) {
        if (length > 0) {
            usb_request_copy_from(req, out_read_buffer, req->response.actual, 0);
        }
        if (out_read_actual != NULL) {
            *out_read_actual = req->response.actual;
        }
    }

    if (use_free_list) {
        fbl::AutoLock lock(&free_reqs_lock_);
        if (usb_request_pool_add(&free_reqs_, req) != ZX_OK) {
            zxlogf(TRACE, "Unable to add back request to the free pool\n");
            usb_request_release(req);
        }
    } else {
        usb_request_release(req);
    }
    return status;
}

zx_status_t UsbDevice::UsbControlOut(uint8_t request_type, uint8_t request, uint16_t value,
                                     uint16_t index, int64_t timeout, const void* write_buffer,
                                     size_t write_size) {
    if ((request_type & USB_DIR_MASK) != USB_DIR_OUT) {
        return ZX_ERR_INVALID_ARGS;
    }
    return Control(request_type, request, value, index, timeout, write_buffer, write_size, nullptr,
                   0, nullptr);
}

zx_status_t UsbDevice::UsbControlIn(uint8_t request_type, uint8_t request, uint16_t value,
                                    uint16_t index, int64_t timeout, void* out_read_buffer,
                                    size_t read_size, size_t* out_read_actual) {
    if ((request_type & USB_DIR_MASK) != USB_DIR_IN) {
        return ZX_ERR_INVALID_ARGS;
    }
    return Control(request_type, request, value, index, timeout, nullptr, 0, out_read_buffer,
                   read_size, out_read_actual);
}

void UsbDevice::UsbRequestQueue(usb_request_t* req, const usb_request_complete_t* complete_cb) {
    UsbRequestInternal* req_int = USB_REQ_TO_DEV_INTERNAL(req, parent_req_size_);
    req_int->complete_cb = *complete_cb;

    req->header.device_id = device_id_;
    // save the existing callback, so we can replace them
    // with our own before passing the request to the HCI driver.
    usb_request_complete_t complete = {
        .callback = [](void* ctx, usb_request_t* req) {
                        static_cast<UsbDevice*>(ctx)->RequestComplete(req);
                    },
        .ctx = this,
    };
    hci_.RequestQueue(req, &complete);
}

zx_status_t UsbDevice::UsbConfigureBatchCallback(uint8_t ep_address,
                                                 const usb_batch_request_complete_t* complete_cb) {
    // TODO(jocelyndang): implement this.
    return ZX_ERR_NOT_SUPPORTED;
}

usb_speed_t UsbDevice::UsbGetSpeed() {
    return speed_;
}

zx_status_t UsbDevice::UsbSetInterface(uint8_t interface_number, uint8_t alt_setting) {
    return Control(USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE, USB_REQ_SET_INTERFACE,
                     alt_setting, interface_number, ZX_TIME_INFINITE, nullptr, 0, nullptr, 0,
                     nullptr);
}

uint8_t UsbDevice::UsbGetConfiguration() {
    fbl::AutoLock lock(&state_lock_);
    auto* descriptor = reinterpret_cast<usb_configuration_descriptor_t*>(
                                                    config_descs_[current_config_index_].get());
    return descriptor->bConfigurationValue;
}

zx_status_t UsbDevice::UsbSetConfiguration(uint8_t configuration) {
    uint8_t index = 0;
    for (auto& desc_array : config_descs_) {
        auto* descriptor = reinterpret_cast<usb_configuration_descriptor_t*>(desc_array.get());
        if (descriptor->bConfigurationValue == configuration) {
            fbl::AutoLock lock(&state_lock_);

            zx_status_t status;
            status = Control(USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                             USB_REQ_SET_CONFIGURATION, configuration, 0, ZX_TIME_INFINITE,
                             nullptr, 0, nullptr,  0, nullptr);
            if (status == ZX_OK) {
                current_config_index_ = index;
            }
            return status;
        }
        index++;
    }
    return ZX_ERR_INVALID_ARGS;
}

zx_status_t UsbDevice::UsbEnableEndpoint(const usb_endpoint_descriptor_t* ep_desc,
                                         const usb_ss_ep_comp_descriptor_t* ss_com_desc,
                                         bool enable) {
    return hci_.EnableEndpoint(device_id_, ep_desc, ss_com_desc, enable);
}

zx_status_t UsbDevice::UsbResetEndpoint(uint8_t ep_address) {
    return hci_.ResetEndpoint(device_id_, ep_address);
}

zx_status_t UsbDevice::UsbResetDevice() {
    {
        fbl::AutoLock lock(&state_lock_);

        if (resetting_) {
            zxlogf(ERROR, "%s: resetting_ already set\n", __func__);
            return ZX_ERR_BAD_STATE;
        }
        resetting_ = true;
   }

    return hci_.ResetDevice(hub_id_, device_id_);
}

size_t UsbDevice::UsbGetMaxTransferSize(uint8_t ep_address) {
    return hci_.GetMaxTransferSize(device_id_, ep_address);
}

uint32_t UsbDevice::UsbGetDeviceId() {
    return device_id_;
}

void UsbDevice::UsbGetDeviceDescriptor(usb_device_descriptor_t* out_desc) {
    memcpy(out_desc, &device_desc_, sizeof(usb_device_descriptor_t));
}

zx_status_t UsbDevice::UsbGetConfigurationDescriptorLength(uint8_t configuration,
                                                           size_t* out_length) {
    for (auto& desc_array : config_descs_) {
        auto* config_desc = reinterpret_cast<usb_configuration_descriptor_t*>(desc_array.get());
        if (config_desc->bConfigurationValue == configuration) {
            *out_length = le16toh(config_desc->wTotalLength);
            return ZX_OK;
        }
    }
     *out_length = 0;
    return ZX_ERR_INVALID_ARGS;
}

zx_status_t UsbDevice::UsbGetConfigurationDescriptor(uint8_t configuration, void* out_desc_buffer,
                                                     size_t desc_size, size_t* out_desc_actual) {
    for (auto& desc_array : config_descs_) {
        auto* config_desc = reinterpret_cast<usb_configuration_descriptor_t*>(desc_array.get());
        if (config_desc->bConfigurationValue == configuration) {
            size_t length = le16toh(config_desc->wTotalLength);
            if (length > desc_size) {
                length = desc_size;
            }
            memcpy(out_desc_buffer, config_desc, length);
            *out_desc_actual = length;
            return ZX_OK;
        }
    }
    return ZX_ERR_INVALID_ARGS;
}


size_t UsbDevice::UsbGetDescriptorsLength() {
    fbl::AutoLock lock(&state_lock_);
    auto* config_desc = reinterpret_cast<usb_configuration_descriptor_t*>(
                                                        config_descs_[current_config_index_].get());
    return le16toh(config_desc->wTotalLength);
}

void UsbDevice::UsbGetDescriptors(void* out_descs_buffer, size_t descs_size,
                                  size_t* out_descs_actual) {
    fbl::AutoLock lock(&state_lock_);
    auto* config_desc = reinterpret_cast<usb_configuration_descriptor_t*>(
                                                        config_descs_[current_config_index_].get());
    size_t length = le16toh(config_desc->wTotalLength);
    if (length > descs_size) {
        length = descs_size;
    }

    memcpy(out_descs_buffer, config_desc, length);
    *out_descs_actual = length;
}

zx_status_t UsbDevice::UsbGetStringDescriptor(uint8_t desc_id, uint16_t lang_id,
                                              uint16_t* out_actual_lang_id, void* buf,
                                          size_t buflen, size_t* out_actual) {
    fbl::AutoLock lock(&state_lock_);

    //  If we have never attempted to load our language ID table, do so now.
    if (!lang_ids_.has_value()) {
        usb_langid_desc_t id_desc;

        size_t actual;
        auto result = GetDescriptor(USB_DT_STRING, 0, 0, &id_desc, sizeof(id_desc), &actual);
        if (result == ZX_ERR_IO_REFUSED || result == ZX_ERR_IO_INVALID) {
            // some devices do not support fetching language list
            // in that case assume US English (0x0409)
            hci_.ResetEndpoint(device_id_, 0);
            id_desc.bLength = 4;
            id_desc.wLangIds[0] = htole16(0x0409);
            actual = 4;
        } else if ((result == ZX_OK) &&
                  ((actual < 4) || (actual != id_desc.bLength) || (actual & 0x1))) {
            return ZX_ERR_INTERNAL;
        }

        // So, if we have managed to fetch/synthesize a language ID table,
        // go ahead and perform a bit of fixup.  Redefine bLength to be the
        // valid number of entires in the table, and fixup the endianness of
        // all the entires in the table.  Then, attempt to swap in the new
        // language ID table.
        if (result == ZX_OK) {
            id_desc.bLength = static_cast<uint8_t>((id_desc.bLength - 2) >> 1);
#if BYTE_ORDER != LITTLE_ENDIAN
            for (uint8_t i = 0; i < id_desc.bLength; ++i) {
                id_desc.wLangIds[i] = letoh16(id_desc.wLangIds[i]);
            }
#endif
        }
        lang_ids_ = id_desc;
    }

    // Handle the special case that the user asked for the language ID table.
    if (desc_id == 0) {
        size_t table_sz = (lang_ids_->bLength << 1);
        buflen &= ~1;
        size_t actual = (table_sz < buflen ? table_sz : buflen);
        memcpy(buf, lang_ids_->wLangIds, actual);
        *out_actual = actual;
        return ZX_OK;
    }

    // Search for the requested language ID.
    uint32_t lang_ndx;
    for (lang_ndx = 0; lang_ndx < lang_ids_->bLength; ++ lang_ndx) {
        if (lang_id == lang_ids_->wLangIds[lang_ndx]) {
            break;
        }
    }

    // If we didn't find it, default to the first entry in the table.
    if (lang_ndx >= lang_ids_->bLength) {
        ZX_DEBUG_ASSERT(lang_ids_->bLength >= 1);
        lang_id = lang_ids_->wLangIds[0];
    }

    usb_string_desc_t string_desc;

    size_t actual;
    auto result = GetDescriptor(USB_DT_STRING, desc_id, le16toh(lang_id), &string_desc,
                                sizeof(string_desc), &actual);

    if (result == ZX_ERR_IO_REFUSED || result == ZX_ERR_IO_INVALID) {
        zx_status_t reset_result = hci_.ResetEndpoint(device_id_, 0);
        if (reset_result != ZX_OK) {
            zxlogf(ERROR, "failed to reset endpoint, err: %d\n", reset_result);
            return result;
        }
        result = GetDescriptor(USB_DT_STRING, desc_id, le16toh(lang_id), &string_desc,
                               sizeof(string_desc), &actual);
        if (result == ZX_ERR_IO_REFUSED || result == ZX_ERR_IO_INVALID) {
            reset_result = hci_.ResetEndpoint(device_id_, 0);
            if (reset_result != ZX_OK) {
                zxlogf(ERROR, "failed to reset endpoint, err: %d\n", reset_result);
                return result;
            }
        }
    }

    if (result != ZX_OK) {
        return result;
    }

    if ((actual < 2) || (actual != string_desc.bLength)) {
        result = ZX_ERR_INTERNAL;
    } else  {
        // Success! Convert this result from UTF16LE to UTF8 and store the
        // language ID we actually fetched (if it was not what the user
        // requested).
        *out_actual = buflen;
        *out_actual_lang_id = lang_id;
        utf16_to_utf8(string_desc.code_points, (string_desc.bLength >> 1) - 1,
                      static_cast<uint8_t*>(buf), out_actual,
                      UTF_CONVERT_FLAG_FORCE_LITTLE_ENDIAN);
        return ZX_OK;
    }

    return result;
}

zx_status_t UsbDevice::UsbCancelAll(uint8_t ep_address) {
    return hci_.CancelAll(device_id_, ep_address);
}

uint64_t UsbDevice::UsbGetCurrentFrame() {
    return hci_.GetCurrentFrame();
}

size_t UsbDevice::UsbGetRequestSize() {
    return req_size_;
}

zx_status_t UsbDevice::MsgGetDeviceSpeed(fidl_txn_t* txn) {
    return fuchsia_hardware_usb_device_DeviceGetDeviceSpeed_reply(txn, speed_);
}

zx_status_t UsbDevice::MsgGetDeviceDescriptor(fidl_txn_t* txn) {
    return fuchsia_hardware_usb_device_DeviceGetDeviceDescriptor_reply(txn,
                                                       reinterpret_cast<uint8_t*>(&device_desc_));
}

zx_status_t UsbDevice::MsgGetConfigurationDescriptorSize(uint8_t config, fidl_txn_t* txn) {
    auto* descriptor = GetConfigDesc(config);
    if (!descriptor) {
        return fuchsia_hardware_usb_device_DeviceGetConfigurationDescriptorSize_reply(
            txn, ZX_ERR_INVALID_ARGS, 0);
    }

    auto length = le16toh(descriptor->wTotalLength);
    return fuchsia_hardware_usb_device_DeviceGetConfigurationDescriptorSize_reply(txn, ZX_OK,
                                                                                  length);
}

zx_status_t UsbDevice::MsgGetConfigurationDescriptor(uint8_t config, fidl_txn_t* txn) {
    auto* descriptor = GetConfigDesc(config);
    if (!descriptor) {
        return fuchsia_hardware_usb_device_DeviceGetConfigurationDescriptor_reply(
            txn, ZX_ERR_INVALID_ARGS, nullptr, 0);
    }

    size_t length = le16toh(descriptor->wTotalLength);
    return fuchsia_hardware_usb_device_DeviceGetConfigurationDescriptor_reply(
        txn, ZX_OK, reinterpret_cast<const uint8_t*>(descriptor), length);
}

zx_status_t UsbDevice::MsgGetStringDescriptor(uint8_t desc_id, uint16_t lang_id, fidl_txn_t* txn) {
    uint8_t buffer[fuchsia_hardware_usb_device_MAX_STRING_DESC_SIZE];
    size_t actual;
    auto status = UsbGetStringDescriptor(desc_id, lang_id, &lang_id, buffer, sizeof(buffer),
                                         &actual);
    return fuchsia_hardware_usb_device_DeviceGetStringDescriptor_reply(txn, status, buffer, actual,
                                                                       lang_id);
}

zx_status_t UsbDevice::MsgSetInterface(uint8_t interface_number, uint8_t alt_setting,
                                     fidl_txn_t* txn) {
    auto status = UsbSetInterface(interface_number, alt_setting);
    return fuchsia_hardware_usb_device_DeviceSetInterface_reply(txn, status);
}

zx_status_t UsbDevice::MsgGetDeviceId(fidl_txn_t* txn) {
    return fuchsia_hardware_usb_device_DeviceGetDeviceId_reply(txn, device_id_);
}

zx_status_t UsbDevice::MsgGetHubDeviceId(fidl_txn_t* txn) {
    return fuchsia_hardware_usb_device_DeviceGetHubDeviceId_reply(txn, hub_id_);
}

zx_status_t UsbDevice::MsgGetConfiguration(fidl_txn_t* txn) {
    fbl::AutoLock lock(&state_lock_);

    auto* descriptor = reinterpret_cast<usb_configuration_descriptor_t*>(
                                                        config_descs_[current_config_index_].get());
    return fuchsia_hardware_usb_device_DeviceGetConfiguration_reply(
        txn, descriptor->bConfigurationValue);
}

zx_status_t UsbDevice::MsgSetConfiguration(uint8_t configuration, fidl_txn_t* txn) {
    auto status = UsbSetConfiguration(configuration);
    return fuchsia_hardware_usb_device_DeviceSetConfiguration_reply(txn, status);
}

static fuchsia_hardware_usb_device_Device_ops_t fidl_ops = {
    .GetDeviceSpeed = [](void* ctx, fidl_txn_t* txn) {
                return reinterpret_cast<UsbDevice*>(ctx)->MsgGetDeviceSpeed(txn); },
    .GetDeviceDescriptor = [](void* ctx, fidl_txn_t* txn) {
                return reinterpret_cast<UsbDevice*>(ctx)->MsgGetDeviceDescriptor(txn); },
    .GetConfigurationDescriptorSize = [](void* ctx, uint8_t config, fidl_txn_t* txn) {
                return reinterpret_cast<UsbDevice*>(ctx)->MsgGetConfigurationDescriptorSize(
                                                            config, txn); },
    .GetConfigurationDescriptor = [](void* ctx, uint8_t config, fidl_txn_t* txn) {
                return reinterpret_cast<UsbDevice*>(ctx)->MsgGetConfigurationDescriptor(
                                                            config, txn); },
    .GetStringDescriptor = [](void* ctx, uint8_t desc_id, uint16_t lang_id, fidl_txn_t* txn) {
                return reinterpret_cast<UsbDevice*>(ctx)->MsgGetStringDescriptor(
                                                            desc_id, lang_id, txn); },
    .SetInterface = [](void* ctx, uint8_t interface_number, uint8_t alt_setting, fidl_txn_t* txn) {
                return reinterpret_cast<UsbDevice*>(ctx)->MsgSetInterface(
                                                            interface_number, alt_setting, txn); },
    .GetDeviceId = [](void* ctx, fidl_txn_t* txn) {
                return reinterpret_cast<UsbDevice*>(ctx)->MsgGetDeviceId(txn); },
    .GetHubDeviceId = [](void* ctx, fidl_txn_t* txn) {
                return reinterpret_cast<UsbDevice*>(ctx)->MsgGetHubDeviceId(txn); },
    .GetConfiguration = [](void* ctx, fidl_txn_t* txn) {
                return reinterpret_cast<UsbDevice*>(ctx)->MsgGetConfiguration(txn); },
    .SetConfiguration = [](void* ctx, uint8_t configuration, fidl_txn_t* txn) {
                return reinterpret_cast<UsbDevice*>(ctx)->MsgSetConfiguration(
                                                            configuration, txn); },
};

zx_status_t UsbDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_usb_device_Device_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t UsbDevice::HubResetPort(uint32_t port) {
    if (!hub_intf_.is_valid()) {
        zxlogf(ERROR, "hub interface not set in usb_bus_reset_hub_port\n");
        return ZX_ERR_BAD_STATE;
    }
    return hub_intf_.ResetPort(port);
}

zx_status_t UsbDevice::Create(zx_device_t* parent, const ddk::UsbHciProtocolClient& hci,
                              uint32_t device_id, uint32_t hub_id, usb_speed_t speed,
                              fbl::RefPtr<UsbDevice>* out_device) {
    fbl::AllocChecker ac;
    auto device = fbl::MakeRefCountedChecked<UsbDevice>(&ac, parent, hci, device_id,
                                                        hub_id, speed);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    // devices_[device_id] must be set before calling DdkAdd().
    *out_device = device;

    auto status = device->Init();
    if (status != ZX_OK) {
        out_device->reset();
    }
    return status;
}

zx_status_t UsbDevice::Init() {
    // We implement ZX_PROTOCOL_USB, but drivers bind to us as ZX_PROTOCOL_USB_DEVICE.
    // We also need this for the device to appear in /dev/class/usb-device/.
    ddk_proto_id_ = ZX_PROTOCOL_USB_DEVICE;

    parent_req_size_ = hci_.GetRequestSize();
    req_size_ = parent_req_size_ + sizeof(UsbRequestInternal);

    list_initialize(&completed_reqs_);
    usb_request_pool_init(&free_reqs_, parent_req_size_ + offsetof(UsbRequestInternal, node));

    // read device descriptor
    size_t actual;
    auto status = GetDescriptor(USB_DT_DEVICE, 0, 0, &device_desc_, sizeof(device_desc_), &actual);
    if (status == ZX_OK && actual != sizeof(device_desc_)) {
        status = ZX_ERR_IO;
    }
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: GetDescriptor(USB_DT_DEVICE) failed\n", __func__);
        return status;
    }

    uint8_t num_configurations = device_desc_.bNumConfigurations;
    fbl::AllocChecker ac;
    config_descs_.reset(new (&ac) fbl::Array<uint8_t>[num_configurations], num_configurations);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    fbl::AutoLock lock(&state_lock_);

    for (uint8_t config = 0; config < num_configurations; config++) {
        // read configuration descriptor header to determine size
        usb_configuration_descriptor_t config_desc_header;
        size_t actual;
        status = GetDescriptor(USB_DT_CONFIG, config, 0, &config_desc_header,
                                         sizeof(config_desc_header), &actual);
        if (status == ZX_OK && actual != sizeof(config_desc_header)) {
            status = ZX_ERR_IO;
        }
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: GetDescriptor(USB_DT_CONFIG) failed\n", __func__);
            return status;
        }
        uint16_t config_desc_size = letoh16(config_desc_header.wTotalLength);
        auto* config_desc = new (&ac) uint8_t[config_desc_size];
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        config_descs_[config].reset(config_desc, config_desc_size);

        // read full configuration descriptor
        status = GetDescriptor(USB_DT_CONFIG, config, 0, config_desc, config_desc_size, &actual);
        if (status == ZX_OK && actual != config_desc_size) {
            status = ZX_ERR_IO;
        }
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: GetDescriptor(USB_DT_CONFIG) failed\n", __func__);
            return status;
        }
    }

    // we will create devices for interfaces on the first configuration by default
    uint8_t configuration = 1;
    const UsbConfigOverride* override = config_overrides;
    while (override->configuration) {
        if (override->vid == le16toh(device_desc_.idVendor) &&
            override->pid == le16toh(device_desc_.idProduct)) {
            configuration = override->configuration;
            break;
        }
        override++;
    }
    if (configuration > num_configurations) {
        zxlogf(ERROR, "usb_device_add: override configuration number out of range\n");
        return ZX_ERR_INTERNAL;
    }
    current_config_index_ = static_cast<uint8_t>(configuration - 1);

    // set configuration
    auto* config_desc = reinterpret_cast<usb_configuration_descriptor_t*>(
                                                    config_descs_[current_config_index_].get());
    status = UsbControlOut(USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                           USB_REQ_SET_CONFIGURATION, config_desc->bConfigurationValue, 0,
                           ZX_TIME_INFINITE, nullptr, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: USB_REQ_SET_CONFIGURATION failed\n", __func__);
        return status;
    }

    zxlogf(INFO, "* found USB device (0x%04x:0x%04x, USB %x.%x) config %u\n",
            device_desc_.idVendor, device_desc_.idProduct, device_desc_.bcdUSB >> 8,
            device_desc_.bcdUSB & 0xff, configuration);

    // Callback thread must be started before device_add() since it will recursively
    // bind other drivers to us before it returns.
    StartCallbackThread();

    char name[16];
    snprintf(name, sizeof(name), "%03d", device_id_);

    zx_device_prop_t props[] = {
        { BIND_USB_VID, 0, device_desc_.idVendor },
        { BIND_USB_PID, 0, device_desc_.idProduct },
        { BIND_USB_CLASS, 0, device_desc_.bDeviceClass },
        { BIND_USB_SUBCLASS, 0, device_desc_.bDeviceSubClass },
        { BIND_USB_PROTOCOL, 0, device_desc_.bDeviceProtocol },
    };

    status = DdkAdd(name, 0, props, countof(props), ZX_PROTOCOL_USB_DEVICE);
    if (status != ZX_OK) {
        return status;
    }

    // Hold a reference while devmgr has a pointer to this object.
    AddRef();

    return ZX_OK;
}

zx_status_t UsbDevice::Reinitialize() {
    fbl::AutoLock lock(&state_lock_);

    if (resetting_) {
        zxlogf(ERROR, "%s: resetting_ not set\n", __func__);
        return ZX_ERR_BAD_STATE;
    }
    resetting_ = false;

    auto* descriptor = reinterpret_cast<usb_configuration_descriptor_t*>(
                                                        config_descs_[current_config_index_].get());
    auto status = UsbControlOut(USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                           USB_REQ_SET_CONFIGURATION, descriptor->bConfigurationValue, 0,
                           ZX_TIME_INFINITE, nullptr, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "could not restore configuration to %u, got err: %d\n",
              descriptor->bConfigurationValue, status);
        return status;
    }

    // TODO(jocelyndang): should we notify the interfaces that the device has been reset?
    // TODO(jocelyndang): we should re-enable endpoints and restore alternate settings.
    return ZX_OK;
}

zx_status_t UsbDevice::GetDescriptor(uint16_t type, uint16_t index, uint16_t language, void* data,
                                     size_t length, size_t* out_actual) {
    return UsbControlIn(USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE, USB_REQ_GET_DESCRIPTOR,
                        static_cast<uint16_t>(type << 8 | index), language, ZX_TIME_INFINITE,
                        data, length, out_actual);
}

} // namespace usb_bus
