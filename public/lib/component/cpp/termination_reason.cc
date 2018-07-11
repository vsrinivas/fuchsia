#include "lib/component/cpp/termination_reason.h"
#include <lib/fxl/strings/string_printf.h>

namespace component {

std::string TerminationReasonToString(
    fuchsia::sys::TerminationReason termination_reason) {
  switch (termination_reason) {
    case fuchsia::sys::TerminationReason::UNKNOWN:
      return "UNKNOWN";
    case fuchsia::sys::TerminationReason::EXITED:
      return "EXITED";
    case fuchsia::sys::TerminationReason::URL_INVALID:
      return "URL_INVALID";
    case fuchsia::sys::TerminationReason::PACKAGE_NOT_FOUND:
      return "PACKAGE_NOT_FOUND";
    case fuchsia::sys::TerminationReason::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
    case fuchsia::sys::TerminationReason::PROCESS_CREATION_ERROR:
      return "PROCESS_CREATION_ERROR";
    case fuchsia::sys::TerminationReason::RUNNER_FAILED:
      return "RUNNER_FAILED";
    case fuchsia::sys::TerminationReason::RUNNER_TERMINATED:
      return "RUNNER_TERMINATED";
    default:
      return fxl::StringPrintf("%d", termination_reason);
  }
}

std::string HumanReadableTerminationReason(
    fuchsia::sys::TerminationReason termination_reason) {
  switch (termination_reason) {
    case fuchsia::sys::TerminationReason::EXITED:
      return "exited";
    case fuchsia::sys::TerminationReason::URL_INVALID:
      return "url invalid";
    case fuchsia::sys::TerminationReason::PACKAGE_NOT_FOUND:
      return "not found";
    case fuchsia::sys::TerminationReason::PROCESS_CREATION_ERROR:
      return "failed to spawn process";
    case fuchsia::sys::TerminationReason::RUNNER_FAILED:
      return "failed to start runner for process";
    case fuchsia::sys::TerminationReason::RUNNER_TERMINATED:
      return "runner failed to execute";
    default:
      return fxl::StringPrintf(
          "failed to create component (%s)",
          TerminationReasonToString(termination_reason).c_str());
  }
}

}  // namespace component
