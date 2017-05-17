// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <limits>
#include <vector>

#include "application/lib/app/application_context.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "lib/ftl/command_line.h"
#include "lib/mtl/tasks/message_loop.h"

namespace flog {

class FlogViewerApp {
 public:
  FlogViewerApp(int argc, const char** argv) {
    std::unique_ptr<app::ApplicationContext> application_context =
        app::ApplicationContext::CreateFromStartupInfo();

    viewer_.Initialize(application_context.get(), []() {
      mtl::MessageLoop::GetCurrent()->PostQuitTask();
    });

    ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);
    bool did_something = false;

    viewer_.set_format(
        command_line.GetOptionValueWithDefault("format", viewer_.format()));

    std::string string_value;
    if (command_line.GetOptionValue("channel", &string_value) ||
        command_line.GetOptionValue("channels", &string_value)) {
      std::vector<uint32_t> channels;
      if (!Parse(string_value, &channels)) {
        Usage();
        return;
      }

      for (uint32_t channel : channels) {
        viewer_.AddChannel(channel);
      }

      did_something = true;
    }

    if (command_line.GetOptionValue("stop-index", &string_value)) {
      uint32_t stop_index;
      if (!Parse(string_value, &stop_index)) {
        Usage();
        return;
      }

      viewer_.set_stop_index(stop_index);

      did_something = true;
    }

    if (command_line.GetOptionValue("last", &string_value)) {
      viewer_.ProcessLastLog(string_value);
      did_something = true;
    }

    for (const std::string& log_id_string : command_line.positional_args()) {
      std::vector<uint32_t> log_ids;
      if (!Parse(log_id_string, &log_ids)) {
        Usage();
        return;
      }

      for (uint32_t log_id : log_ids) {
        viewer_.ProcessLog(log_id);
      }

      did_something = true;
    }

    if (command_line.HasOption("delete-all-logs")) {
      viewer_.DeleteAllLogs();
    } else if (command_line.GetOptionValue("delete-log", &string_value) ||
               command_line.GetOptionValue("delete-logs", &string_value)) {
      std::vector<uint32_t> logs;
      if (!Parse(string_value, &logs)) {
        Usage();
        return;
      }

      for (uint32_t log : logs) {
        viewer_.DeleteLog(log);
      }

      did_something = true;
    }

    if (!did_something) {
      viewer_.ProcessLogs();
    }
  }

 private:
  void Usage() {
    std::cout << std::endl;
    std::cout << "fidl:flog_viewer <args>\"" << std::endl;
    std::cout << "    <log id>                    process specified log"
              << std::endl;
    std::cout << "    --last=<label>              process last log with "
                 "specified label"
              << std::endl;
    std::cout
        << "    --last                      process last log with any label"
        << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << "    --format=<format>           terse (default), full, digest"
              << std::endl;
    std::cout
        << "    --channel(s)=<channel ids>  process only the indicated channels"
        << std::endl;
    std::cout << "    --stop-index=<time>         process up to the indicated "
                 "entry index"
              << std::endl;
    std::cout << "    --delete-log(s)=<log ids>   delete the indicated logs"
              << std::endl;
    std::cout << "    --delete-all-logs           delete all logs" << std::endl;
    std::cout << "If no log is specified, a list of logs is printed."
              << std::endl;
    std::cout << "Value lists are comma-separated (channel ids, log ids)."
              << std::endl;
    std::cout << std::endl;

    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }

  bool Parse(const std::string& string_value,
             std::vector<uint32_t>* vector_of_uint32_out) {
    FTL_DCHECK(vector_of_uint32_out);

    std::istringstream istream(string_value);
    uint32_t value;
    while (istream >> value) {
      vector_of_uint32_out->push_back(value);
      if (istream.peek() == ',') {
        istream.ignore();
      }
    }

    return vector_of_uint32_out->size() != 0 && istream.eof();
  }

  bool Parse(const std::string& string_value, uint32_t* uint32_out) {
    FTL_DCHECK(uint32_out);

    std::istringstream istream(string_value);
    return (istream >> *uint32_out) && istream.eof();
  }

  FlogViewer viewer_;
};

}  // namespace flog

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;

  flog::FlogViewerApp app(argc, argv);

  loop.Run();
  return 0;
}
