// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-device.h"

#include <fuchsia/hardware/usb/device/llcpp/fidl.h>
#include <zircon/hw/usb.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb/bus.h>
#include <ddktl/fidl.h>
#include <fbl/auto_lock.h>
#include <utf_conversion/utf_conversion.h>

#include "usb-bus.h"

namespace usb_bus {

class UsbWaiterImpl : public UsbWaiterInterface {
 public:
  zx_status_t Wait(sync_completion_t* completion, zx_duration_t duration) {
    return sync_completion_wait(completion, duration);
  }
};

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
    {0x0bda, 0x8153, 2},  // Realtek ethernet dongle has CDC interface on configuration 2
    {0, 0, 0},
};

// This thread is for calling the usb request completion callback for requests received from our
// client. We do this on a separate thread because it is unsafe to call out on our own completion
// callback, which is called on the main thread of the USB HCI driver.
int UsbDevice::CallbackThread() {
  bool done = false;

  while (!done) {
    UnownedRequestQueue temp_queue;

    // Wait for new usb requests to complete or for signal to exit this thread.
    sync_completion_wait(&callback_thread_completion_, ZX_TIME_INFINITE);
    sync_completion_reset(&callback_thread_completion_);

    {
      fbl::AutoLock lock(&callback_lock_);

      done = callback_thread_stop_;

      // Copy completed requests to a temp list so we can process them outside of our lock.
      temp_queue = std::move(completed_reqs_);
    }

    // Call completion callbacks outside of the lock.
    for (auto req = temp_queue.pop(); req; req = temp_queue.pop()) {
      if (req->operation()->reset) {
        req->Complete(hci_.ResetEndpoint(device_id_, req->operation()->reset_address), 0,
                      req->private_storage()->silent_completions_count);
        continue;
      }
      const auto& response = req->request()->response;
      req->Complete(response.status, response.actual,
                    req->private_storage()->silent_completions_count);
    }
  }

  return 0;
}

void UsbDevice::StartCallbackThread() {
  // TODO(voydanoff) Once we have a way of knowing when a driver has bound to us, move the thread
  // start there so we don't have to start a thread unless we know we will need it.
  thrd_create_with_name(
      &callback_thread_,
      [](void* arg) -> int { return static_cast<UsbDevice*>(arg)->CallbackThread(); }, this,
      "usb-device-callback-thread");
}

void UsbDevice::StopCallbackThread() {
  {
    fbl::AutoLock lock(&callback_lock_);
    callback_thread_stop_ = true;
  }

  sync_completion_signal(&callback_thread_completion_);
  thrd_join(callback_thread_, nullptr);
}

UsbDevice::Endpoint* UsbDevice::GetEndpoint(uint8_t ep_address) {
  uint8_t index = 0;
  if (ep_address > 0) {
    index = static_cast<uint8_t>(2 * (ep_address & ~USB_ENDPOINT_DIR_MASK));
    if ((ep_address & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_OUT) {
      index--;
    }
  }
  return index < USB_MAX_EPS ? &eps_[index] : nullptr;
}

bool UsbDevice::UpdateEndpoint(Endpoint* ep, usb_request_t* completed_req) {
  fbl::AutoLock lock(&ep->lock);

  auto unowned = UnownedRequest(completed_req, parent_req_size_, /* allow_destruct */ false);

  std::optional<size_t> completed_req_idx = ep->pending_reqs.find(&unowned);
  if (!completed_req_idx.has_value()) {
    zxlogf(ERROR, "could not find completed req %p in pending list of endpoint: 0x%x\n",
           unowned.request(), completed_req->header.ep_address);
    // This should never happen, but we should probably still do a callback.
    return true;
  }

  auto* storage = unowned.private_storage();
  storage->ready_for_client = true;

  auto opt_prev = ep->pending_reqs.prev(&unowned);
  // If all requests in the pending list prior to this one are ready for a callback,
  // then this request has completed in order. Since we do an immediate callback
  // for out of order requests, we just have to check the request before this one.
  bool completed_in_order = !opt_prev.has_value() || opt_prev->private_storage()->ready_for_client;

  if (!storage->require_callback && completed_in_order && completed_req->response.status == ZX_OK) {
    // Skipping unwanted callback since the request completed successfully and in order.
    // Don't remove the request from the list until we do the next callback.
    return false;
  }

  if (completed_in_order) {
    // Remove all requests up to the current request from the pending list.
    auto opt_req = ep->pending_reqs.begin();
    while (opt_req) {
      auto req = *std::move(opt_req);
      auto opt_next = ep->pending_reqs.next(&req);

      ZX_DEBUG_ASSERT(req.private_storage()->ready_for_client);

      ep->pending_reqs.erase(&req);
      if (req.request() == completed_req) {
        break;
      }
      opt_req = std::move(opt_next);
    }
  } else {
    // The request completed out of order. Only remove the single request.
    ep->pending_reqs.erase(&unowned);
    // If this request was supposed to do a callback, make sure the previous request
    // will do a callback.
    ZX_DEBUG_ASSERT(opt_prev.has_value());  // Must be populated if we completed out of order.
    if (unowned.private_storage()->require_callback) {
      opt_prev->private_storage()->require_callback = true;
    }
  }
  unowned.private_storage()->silent_completions_count =
      completed_in_order ? completed_req_idx.value() : 0;
  return true;
}

// usb request completion for the requests passed down to the HCI driver
void UsbDevice::RequestComplete(usb_request_t* req) {
  if (req->reset) {
    QueueCallback(req);
    return;
  }
  auto* ep = GetEndpoint(req->header.ep_address);
  if (!ep) {
    zxlogf(ERROR, "could not find endpoint with address 0x%x\n", req->header.ep_address);
    // This should never happen, but we should probably still do a callback.
    QueueCallback(req);
    return;
  }

  bool do_callback = UpdateEndpoint(ep, req);
  if (do_callback) {
    QueueCallback(req);
  }
}

void UsbDevice::QueueCallback(usb_request_t* req) {
  {
    fbl::AutoLock lock(&callback_lock_);

    // move original request to completed_reqs list so it can be completed on the callback_thread
    completed_reqs_.push(UnownedRequest(req, parent_req_size_));
  }
  sync_completion_signal(&callback_thread_completion_);
}

void UsbDevice::SetHubInterface(const usb_hub_interface_protocol_t* hub_intf) {
  if (hub_intf) {
    hub_intf_ = hub_intf;
  } else {
    hub_intf_.clear();
  }
}

const usb_configuration_descriptor_t* UsbDevice::GetConfigDesc(uint8_t config) {
  for (auto& desc_array : config_descs_) {
    auto* desc = reinterpret_cast<usb_configuration_descriptor_t*>(desc_array.data());
    if (desc->bConfigurationValue == config) {
      return desc;
    }
  }
  return nullptr;
}

zx_status_t UsbDevice::DdkGetProtocol(uint32_t proto_id, void* protocol) {
  zx_status_t status;

  switch (proto_id) {
    case ZX_PROTOCOL_USB: {
      auto* usb_proto = static_cast<usb_protocol_t*>(protocol);
      usb_proto->ctx = this;
      usb_proto->ops = &usb_protocol_ops_;
      status = ZX_OK;
      break;
    }
    case ZX_PROTOCOL_USB_BUS: {
      auto* bus_proto = static_cast<usb_bus_protocol_t*>(protocol);
      bus_.GetProto(bus_proto);
      status = ZX_OK;
      break;
    }
    default:
      status = ZX_ERR_NOT_SUPPORTED;
      break;
  }

  return status;
}

void UsbDevice::DdkUnbindDeprecated() { DdkRemoveDeprecated(); }

void UsbDevice::DdkRelease() {
  StopCallbackThread();

  if (Release()) {
    delete this;
  }
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

  std::optional<Request> req;
  bool use_free_list = (length == 0);
  if (use_free_list) {
    req = free_reqs_.Get(length);
  }

  if (!req.has_value()) {
    auto status = Request::Alloc(&req, length, 0, parent_req_size_);
    if (status != ZX_OK) {
      return status;
    }
  }

  // fill in protocol data
  usb_setup_t* setup = &req->request()->setup;
  setup->bmRequestType = request_type;
  setup->bRequest = request;
  setup->wValue = value;
  setup->wIndex = index;
  setup->wLength = static_cast<uint16_t>(length);

  if (out) {
    if (length > 0 && write_buffer == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (length > write_size) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
  } else {
    if (length > 0 && out_read_buffer == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    if (length > read_size) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
  }

  if (length > 0 && out) {
    req->CopyTo(write_buffer, length, 0);
  }

  sync_completion_t completion;

  req->request()->header.device_id = device_id_;
  req->request()->header.length = length;
  // We call this directly instead of via hci_queue, as it's safe to call our
  // own completion callback, and prevents clients getting into odd deadlocks.
  usb_request_complete_t complete = {
      .callback = ControlComplete,
      .ctx = &completion,
  };
  // Use request() instead of take() since we keep referring to the request below.
  hci_.RequestQueue(req->request(), &complete);
  auto status = waiter_->Wait(&completion, timeout);

  if (status == ZX_OK) {
    status = req->request()->response.status;
  } else if (status == ZX_ERR_TIMED_OUT) {
    // cancel transactions and wait for request to be completed
    sync_completion_reset(&completion);
    status = hci_.CancelAll(device_id_, 0);
    if (status == ZX_OK) {
      waiter_->Wait(&completion, ZX_TIME_INFINITE);
      status = ZX_ERR_TIMED_OUT;
    }
  }
  if (status == ZX_OK && !out) {
    auto actual = req->request()->response.actual;
    if (length > 0) {
      req->CopyFrom(out_read_buffer, actual, 0);
    }
    if (out_read_actual != nullptr) {
      *out_read_actual = actual;
    }
  }

  if (use_free_list) {
    free_reqs_.Add(std::move(*req));
  } else {
    req->Release();
  }
  return status;
}

zx_status_t UsbDevice::UsbControlOut(uint8_t request_type, uint8_t request, uint16_t value,
                                     uint16_t index, int64_t timeout, const void* write_buffer,
                                     size_t write_size) {
  if ((request_type & USB_DIR_MASK) != USB_DIR_OUT) {
    return ZX_ERR_INVALID_ARGS;
  }
  return Control(request_type, request, value, index, timeout, write_buffer, write_size, nullptr, 0,
                 nullptr);
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
  req->header.device_id = device_id_;
  if (req->reset) {
    // Save client's callback in private storage.
    UnownedRequest request(req, *complete_cb, parent_req_size_);
    *request.private_storage() = {.ready_for_client = false,
                                  .require_callback = !req->cb_on_error_only,
                                  .silent_completions_count = 0};
    RequestComplete(request.take());
    return;
  }
  if (req->direct) {
    hci_.RequestQueue(req, complete_cb);
    return;
  }

  // Queue to HCI driver with our own completion callback so we can call client's completion
  // on our own thread, to avoid drivers from deadlocking the HCI driver's interrupt thread.
  usb_request_complete_t complete = {
      .callback = [](void* ctx,
                     usb_request_t* req) { static_cast<UsbDevice*>(ctx)->RequestComplete(req); },
      .ctx = this,
  };

  // Save client's callback in private storage.
  UnownedRequest request(req, *complete_cb, parent_req_size_);
  *request.private_storage() = {.ready_for_client = false,
                                .require_callback = !req->cb_on_error_only,
                                .silent_completions_count = 0};

  auto* ep = GetEndpoint(req->header.ep_address);
  if (!ep) {
    zxlogf(ERROR, "could not find endpoint with address 0x%x\n", req->header.ep_address);
  }

  {
    // RequestQueue may callback before it returns, so make sure to release the endpoint lock.
    fbl::AutoLock lock(&ep->lock);
    ep->pending_reqs.push_back(&request);
  }

  // Queue with our callback instead.
  hci_.RequestQueue(request.take(), &complete);
}

usb_speed_t UsbDevice::UsbGetSpeed() { return speed_; }

zx_status_t UsbDevice::UsbSetInterface(uint8_t interface_number, uint8_t alt_setting) {
  return Control(USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE, USB_REQ_SET_INTERFACE,
                 alt_setting, interface_number, ZX_TIME_INFINITE, nullptr, 0, nullptr, 0, nullptr);
}

uint8_t UsbDevice::UsbGetConfiguration() {
  fbl::AutoLock lock(&state_lock_);
  auto* descriptor = reinterpret_cast<usb_configuration_descriptor_t*>(
      config_descs_[current_config_index_].data());
  return descriptor->bConfigurationValue;
}

zx_status_t UsbDevice::UsbSetConfiguration(uint8_t configuration) {
  uint8_t index = 0;
  for (auto& desc_array : config_descs_) {
    auto* descriptor = reinterpret_cast<usb_configuration_descriptor_t*>(desc_array.data());
    if (descriptor->bConfigurationValue == configuration) {
      fbl::AutoLock lock(&state_lock_);

      zx_status_t status;
      status =
          Control(USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE, USB_REQ_SET_CONFIGURATION,
                  configuration, 0, ZX_TIME_INFINITE, nullptr, 0, nullptr, 0, nullptr);
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

uint32_t UsbDevice::UsbGetDeviceId() { return device_id_; }

void UsbDevice::UsbGetDeviceDescriptor(usb_device_descriptor_t* out_desc) {
  memcpy(out_desc, &device_desc_, sizeof(usb_device_descriptor_t));
}

zx_status_t UsbDevice::UsbGetConfigurationDescriptorLength(uint8_t configuration,
                                                           size_t* out_length) {
  for (auto& desc_array : config_descs_) {
    auto* config_desc = reinterpret_cast<usb_configuration_descriptor_t*>(desc_array.data());
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
    auto* config_desc = reinterpret_cast<usb_configuration_descriptor_t*>(desc_array.data());
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
      config_descs_[current_config_index_].data());
  return le16toh(config_desc->wTotalLength);
}

void UsbDevice::UsbGetDescriptors(void* out_descs_buffer, size_t descs_size,
                                  size_t* out_descs_actual) {
  fbl::AutoLock lock(&state_lock_);
  auto* config_desc = reinterpret_cast<usb_configuration_descriptor_t*>(
      config_descs_[current_config_index_].data());
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
  for (lang_ndx = 0; lang_ndx < lang_ids_->bLength; ++lang_ndx) {
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
  } else {
    // Success! Convert this result from UTF16LE to UTF8 and store the
    // language ID we actually fetched (if it was not what the user
    // requested).
    *out_actual = buflen;
    *out_actual_lang_id = lang_id;
    utf16_to_utf8(string_desc.code_points, (string_desc.bLength >> 1) - 1,
                  static_cast<uint8_t*>(buf), out_actual, UTF_CONVERT_FLAG_FORCE_LITTLE_ENDIAN);
    return ZX_OK;
  }

  return result;
}

zx_status_t UsbDevice::UsbCancelAll(uint8_t ep_address) {
  zx_status_t status = hci_.CancelAll(device_id_, ep_address);
  if (status != ZX_OK) {
    return status;
  }
  // Stop the callback thread to prevent races.
  StopCallbackThread();
  // Complete all outstanding requests (host controller should have invoked all of the callbacks
  // at this layer in the stack.
  UnownedRequestQueue temp_queue;
  {
    fbl::AutoLock lock(&callback_lock_);
    // Copy completed requests to a temp list so we can process them outside of our lock.
    temp_queue = std::move(completed_reqs_);
  }
  // Call completion callbacks outside of the lock.
  for (auto req = temp_queue.pop(); req; req = temp_queue.pop()) {
    if (req->operation()->reset) {
      req->Complete(hci_.ResetEndpoint(device_id_, req->operation()->reset_address), 0,
                    req->private_storage()->silent_completions_count);
      continue;
    }
    const auto& response = req->request()->response;
    req->Complete(response.status, response.actual,
                  req->private_storage()->silent_completions_count);
  }

  // TODO(jocelyndang): after cancelling, we should check if the ep pending_reqs has any items.
  // We may have to do callbacks now if the requests already completed before the cancel
  // occurred, but the client did not request any callbacks.

  {
    fbl::AutoLock lock(&callback_lock_);
    callback_thread_stop_ = false;
    StartCallbackThread();
  }
  return ZX_OK;
}

uint64_t UsbDevice::UsbGetCurrentFrame() { return hci_.GetCurrentFrame(); }

size_t UsbDevice::UsbGetRequestSize() { return UnownedRequest::RequestSize(parent_req_size_); }

void UsbDevice::GetDeviceSpeed(GetDeviceSpeedCompleter::Sync completer) { completer.Reply(speed_); }

void UsbDevice::GetDeviceDescriptor(GetDeviceDescriptorCompleter::Sync completer) {
  fidl::Array<uint8_t, sizeof(device_desc_)> data;
  memcpy(data.data(), &device_desc_, sizeof(device_desc_));
  completer.Reply(std::move(data));
}

void UsbDevice::GetConfigurationDescriptorSize(
    uint8_t config, GetConfigurationDescriptorSizeCompleter::Sync completer) {
  auto* descriptor = GetConfigDesc(config);
  if (!descriptor) {
    completer.Reply(ZX_ERR_INVALID_ARGS, 0);
  }

  auto length = le16toh(descriptor->wTotalLength);
  return completer.Reply(ZX_OK, length);
}

void UsbDevice::GetConfigurationDescriptor(uint8_t config,
                                           GetConfigurationDescriptorCompleter::Sync completer) {
  auto* descriptor = GetConfigDesc(config);
  if (!descriptor) {
    return completer.Reply(ZX_ERR_INVALID_ARGS, fidl::VectorView<uint8_t>());
  }

  size_t length = le16toh(descriptor->wTotalLength);
  return completer.Reply(
      ZX_OK, fidl::VectorView<uint8_t>(
                 const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(descriptor)), length));
}

void UsbDevice::GetStringDescriptor(uint8_t desc_id, uint16_t lang_id,
                                    GetStringDescriptorCompleter::Sync completer) {
  char buffer[llcpp::fuchsia::hardware::usb::device::MAX_STRING_DESC_SIZE];
  size_t actual;
  auto status = UsbGetStringDescriptor(desc_id, lang_id, &lang_id, buffer, sizeof(buffer), &actual);
  return completer.Reply(status, fidl::StringView(buffer, actual), lang_id);
}

void UsbDevice::SetInterface(uint8_t interface_number, uint8_t alt_setting,
                             SetInterfaceCompleter::Sync completer) {
  auto status = UsbSetInterface(interface_number, alt_setting);
  completer.Reply(status);
}

void UsbDevice::GetDeviceId(GetDeviceIdCompleter::Sync completer) {
  return completer.Reply(device_id_);
}

void UsbDevice::GetHubDeviceId(GetHubDeviceIdCompleter::Sync completer) {
  completer.Reply(hub_id_);
}

void UsbDevice::GetConfiguration(GetConfigurationCompleter::Sync completer) {
  fbl::AutoLock lock(&state_lock_);

  auto* descriptor = reinterpret_cast<usb_configuration_descriptor_t*>(
      config_descs_[current_config_index_].data());
  completer.Reply(descriptor->bConfigurationValue);
}

void UsbDevice::SetConfiguration(uint8_t configuration, SetConfigurationCompleter::Sync completer) {
  auto status = UsbSetConfiguration(configuration);
  completer.Reply(status);
}

zx_status_t UsbDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::usb::device::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
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
  auto device = fbl::MakeRefCountedChecked<UsbDevice>(&ac, parent, hci, device_id, hub_id, speed,
                                                      fbl::MakeRefCounted<UsbWaiterImpl>());
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
      config_descs_[current_config_index_].data());
  status =
      UsbControlOut(USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE, USB_REQ_SET_CONFIGURATION,
                    config_desc->bConfigurationValue, 0, ZX_TIME_INFINITE, nullptr, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: USB_REQ_SET_CONFIGURATION failed\n", __func__);
    return status;
  }
  zxlogf(INFO, "* found USB device (0x%04x:0x%04x, USB %x.%x) config %u\n", device_desc_.idVendor,
         device_desc_.idProduct, device_desc_.bcdUSB >> 8, device_desc_.bcdUSB & 0xff,
         configuration);

  // Callback thread must be started before device_add() since it will recursively
  // bind other drivers to us before it returns.
  StartCallbackThread();

  char name[16];
  snprintf(name, sizeof(name), "%03d", device_id_);

  zx_device_prop_t props[] = {
      {BIND_USB_VID, 0, device_desc_.idVendor},
      {BIND_USB_PID, 0, device_desc_.idProduct},
      {BIND_USB_CLASS, 0, device_desc_.bDeviceClass},
      {BIND_USB_SUBCLASS, 0, device_desc_.bDeviceSubClass},
      {BIND_USB_PROTOCOL, 0, device_desc_.bDeviceProtocol},
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
      config_descs_[current_config_index_].data());
  auto status =
      UsbControlOut(USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE, USB_REQ_SET_CONFIGURATION,
                    descriptor->bConfigurationValue, 0, ZX_TIME_INFINITE, nullptr, 0);
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
                      static_cast<uint16_t>(type << 8 | index), language, ZX_TIME_INFINITE, data,
                      length, out_actual);
}

}  // namespace usb_bus
