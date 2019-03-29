#include <lib/ui/input/input_device_impl.h>

#include <src/lib/fxl/logging.h>
#include <trace/event.h>

namespace mozart {

InputDeviceImpl::InputDeviceImpl(
    uint32_t id, fuchsia::ui::input::DeviceDescriptor descriptor,
    fidl::InterfaceRequest<fuchsia::ui::input::InputDevice>
        input_device_request,
    Listener* listener)
    : id_(id),
      descriptor_(std::move(descriptor)),
      input_device_binding_(this, std::move(input_device_request)),
      listener_(listener) {
  input_device_binding_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(INFO) << "Device disconnected";
    listener_->OnDeviceDisconnected(this);
  });
}

InputDeviceImpl::~InputDeviceImpl() {}

void InputDeviceImpl::DispatchReport(fuchsia::ui::input::InputReport report) {
  TRACE_DURATION("input", "input_report_listener", "id", report.trace_id);
  TRACE_FLOW_END("input", "hid_read_to_listener", report.trace_id);
  TRACE_FLOW_BEGIN("input", "report_to_presenter", report.trace_id);
  listener_->OnReport(this, std::move(report));
}

}  // namespace mozart
