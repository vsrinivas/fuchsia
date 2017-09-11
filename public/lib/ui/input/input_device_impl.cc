#include "lib/ui/input/input_device_impl.h"

#include "lib/fxl/logging.h"

namespace mozart {

InputDeviceImpl::InputDeviceImpl(
    uint32_t id,
    mozart::DeviceDescriptorPtr descriptor,
    fidl::InterfaceRequest<mozart::InputDevice> input_device_request,
    Listener* listener)
    : id_(id),
      descriptor_(std::move(descriptor)),
      input_device_binding_(this, std::move(input_device_request)),
      listener_(listener) {
  input_device_binding_.set_connection_error_handler([this] {
    FXL_LOG(INFO) << "Device disconnected";
    listener_->OnDeviceDisconnected(this);
  });
}

InputDeviceImpl::~InputDeviceImpl() {}

void InputDeviceImpl::DispatchReport(mozart::InputReportPtr report) {
  listener_->OnReport(this, std::move(report));
}

}  // namespace mozart
