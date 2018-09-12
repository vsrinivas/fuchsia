#ifndef PERIDOT_BIN_SESSIONCTL_SESSION_CTL_APP_H_
#define PERIDOT_BIN_SESSIONCTL_SESSION_CTL_APP_H_

#include <iostream>
#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/future.h>
#include <lib/async/cpp/task.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/strings/string_printf.h>
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/sessionctl/logger.h"

using ::fuchsia::modular::PuppetMaster;
using ::fuchsia::modular::PuppetMasterPtr;

namespace modular {
class SessionCtlApp {
 public:
  explicit SessionCtlApp(fuchsia::modular::PuppetMaster* const puppet_master,
                         const fxl::CommandLine& command_line,
                         async::Loop* const loop,
                         const modular::Logger& logger);

  std::string ExecuteAddModCommand();
  std::string ExecuteRemoveModCommand();

 private:
  // Focus the story to which the mod we are adding belongs.
  fuchsia::modular::StoryCommand MakeFocusStoryCommand();

  // Focus the mod we just added. This is not necessary when adding a new mod
  // since it will be always focused. However, when a mod is updated it might
  // not be focused.
  fuchsia::modular::StoryCommand MakeFocusModCommand(
      const std::string& mod_name);

  fidl::VectorPtr<fuchsia::modular::StoryCommand> MakeAddModCommands(
      const std::string& mod_url, const std::string& mod_name);

  fidl::VectorPtr<fuchsia::modular::StoryCommand> MakeRemoveModCommands(
      const std::string& mod_name);

  modular::FuturePtr<bool, std::string> ExecuteAction(
      fidl::VectorPtr<fuchsia::modular::StoryCommand> commands,
      const std::string& story_name);

  std::string GenerateMissingFlagString(
      const std::vector<std::string>& missing_flags);

  fuchsia::modular::PuppetMaster* const puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
  const fxl::CommandLine command_line_;
  async::Loop* const loop_;
  const modular::Logger logger_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONCTL_SESSION_CTL_APP_H_
