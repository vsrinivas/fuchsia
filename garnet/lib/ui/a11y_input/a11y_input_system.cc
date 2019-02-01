#include "a11y_input_system.h"

namespace scenic_impl {
namespace a11y_input {

A11yInputSystem::A11yInputSystem(SystemContext context,
                                 bool initialized_after_construction)
    : System(std::move(context), initialized_after_construction) {
  FXL_LOG(INFO) << "Scenic accessibility input system started.";
}

std::unique_ptr<CommandDispatcher> A11yInputSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return nullptr;
}

}  // namespace a11y_input
}  // namespace scenic_impl
