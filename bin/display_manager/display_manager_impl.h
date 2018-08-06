#ifndef GARNET_BIN_DISPLAY_MANAGER_DISPLAY_MANAGER_IMPL_H_
#define GARNET_BIN_DISPLAY_MANAGER_DISPLAY_MANAGER_IMPL_H_

#include <fuchsia/device/display/cpp/fidl.h>

#include "display.h"
#include "lib/component/cpp/startup_context.h"

namespace display {

// This class is a thin wrapper around a Display object, implementing
// the DisplayManager FIDL interface.
class DisplayManagerImpl : public fuchsia::device::display::Manager {
 public:
  DisplayManagerImpl();
  virtual void GetBrightness(GetBrightnessCallback callback);
  virtual void SetBrightness(double brightness, SetBrightnessCallback callback);

 protected:
  DisplayManagerImpl(std::unique_ptr<component::StartupContext> context);

 private:
  DisplayManagerImpl(const DisplayManagerImpl&) = delete;
  DisplayManagerImpl& operator=(const DisplayManagerImpl&) = delete;

  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<Manager> bindings_;
  std::unique_ptr<Display> display_;
};
}  // namespace display

#endif  // GARNET_BIN_DISPLAY_MANAGER_DISPLAY_MANAGER_IMPL_H_
