#include "peridot/bin/sessionctl/logger.h"

namespace modular {

Logger::Logger(bool json_out) : json_out_(json_out) {}

void Logger::LogError(const std::string& command,
                      const std::string& error) const {
  if (json_out_) {
    std::string json_output =
        R"JSON(
          {
            "success":false, "command":"%s", "error":"%s", "params":{}
          })JSON";

    std::cout << fxl::StringPrintf(json_output.c_str(), command.c_str(),
                                   error.c_str())
              << std::endl;
  } else {
    std::cout << error << std::endl;
  }
}

void Logger::Log(const std::string& command,
                 const std::map<std::string, std::string>& params) const {
  if (json_out_) {
    std::string json_output =
        R"JSON(
          {
            "success":true,
            "command":"%s",
            "params":{%s}
          })JSON";

    // Generate params string. Ex. "mod_name": "mod1", "story_name": "story1"
    std::string params_string;
    for (auto p = params.begin(); p != params.end(); p++) {
      std::string param_string;

      // Prevent trailing comma in params string when last element of map.
      if (p == --params.end()) {
        param_string = R"("%s" : "%s")";
      } else {
        param_string = R"("%s" : "%s", )";
      }
      params_string += fxl::StringPrintf(param_string.c_str(), p->first.c_str(),
                                         p->second.c_str());
    }

    std::cout << fxl::StringPrintf(json_output.c_str(), command.c_str(),
                                   params_string.c_str())
              << std::endl;
  } else {
    if (command == "add_mod") {
      std::cout << "Created";
    } else if (command == "remove_mod") {
      std::cout << "Removed";
    }

    std::cout << " mod_name: " << params.at("mod_name").c_str()
              << " in story_name: " << params.at("story_name").c_str()
              << std::endl;
  }
}
}  // namespace modular
