#ifndef GARNET_LIB_UI_A11Y_INPUT_A11Y_INPUT_SYSTEM_H_
#define GARNET_LIB_UI_A11Y_INPUT_A11Y_INPUT_SYSTEM_H_

#include <memory>

#include "garnet/lib/ui/scenic/system.h"

namespace scenic_impl {
namespace a11y_input {

class A11yInputSystem : public System {
 public:
  static constexpr TypeId kTypeId = kA11yInput;
  static const char* kName;

  explicit A11yInputSystem(SystemContext context,
                           bool initialized_after_construction);
  ~A11yInputSystem() override = default;

  virtual CommandDispatcherUniquePtr CreateCommandDispatcher(
      CommandDispatcherContext context) override;

 private:
};
}  // namespace a11y_input
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_A11Y_INPUT_A11Y_INPUT_SYSTEM_H_
