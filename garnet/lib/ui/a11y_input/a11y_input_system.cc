#include "a11y_input_system.h"

namespace scenic_impl {
namespace a11y_input {

const char* A11yInputSystem::kName = "A11yInputSystem";

A11yInputSystem::A11yInputSystem(SystemContext context,
                                 bool initialized_after_construction)
    : System(std::move(context), initialized_after_construction) {
  FXL_LOG(INFO) << "Scenic accessibility input system started.";
}

CommandDispatcherUniquePtr A11yInputSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return CommandDispatcherUniquePtr(/* command dispatcher */ nullptr,
                                    /* custom deleter */ nullptr);
}

}  // namespace a11y_input
}  // namespace scenic_impl
