// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <limits>
#include <vector>

#include "lib/app/cpp/application_context.h"
#include "garnet/bin/flog_viewer/channel_handler.h"
#include "garnet/bin/flog_viewer/flog_viewer.h"
#include "lib/ftl/command_line.h"
#include "lib/mtl/tasks/message_loop.h"

namespace flog {

// Extracts a pair of uint32_ts from an istream. Accepts first.second or just
// second, in which case first is set to 0.
std::istream& operator>>(std::istream& istream,
                         std::pair<uint32_t, uint32_t>& value) {
  uint32_t first;
  if (istream >> first) {
    uint32_t second;
    if (istream.peek() != '.') {
      value = std::pair<uint32_t, uint32_t>(0, first);
    } else if (istream.ignore() && istream >> second) {
      value = std::pair<uint32_t, uint32_t>(first, second);
    }
  }

  return istream;
}

// Extracts a comma-separated list as a vector.
template <typename T>
std::istream& operator>>(std::istream& istream, std::vector<T>& value_vector) {
  T value;

  while (istream >> value) {
    value_vector.push_back(value);

    if (istream.eof() || istream.peek() != ',') {
      break;
    }

    istream.ignore();
  }

  return istream;
}

// Parses a string to produce the specified value.
template <typename T>
bool Parse(const std::string& string_value, T* value_out) {
  FTL_DCHECK(value_out);

  std::istringstream istream(string_value);
  return (istream >> *value_out) && istream.eof();
}

class FlogViewerApp {
 public:
  FlogViewerApp(int argc, const char** argv) {
    std::unique_ptr<app::ApplicationContext> application_context =
        app::ApplicationContext::CreateFromStartupInfo();

    viewer_.Initialize(application_context.get(), []() {
      mtl::MessageLoop::GetCurrent()->PostQuitTask();
    });

    ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);

    std::vector<uint32_t> log_ids;
    for (const std::string& log_id_string : command_line.positional_args()) {
      if (!Parse(log_id_string, &log_ids)) {
        std::cout << "Failed to parse log ids.\n";
        Usage();
        return;
      }
    }

    viewer_.set_format(
        command_line.GetOptionValueWithDefault("format", viewer_.format()));

    std::string string_value;
    if (command_line.GetOptionValue("channel", &string_value) ||
        command_line.GetOptionValue("channels", &string_value)) {
      if (log_ids.size() == 0) {
        std::cout << "--channel(s) option not applicable.\n";
        Usage();
        return;
      }

      std::vector<std::pair<uint32_t, uint32_t>> channels;
      if (!Parse(string_value, &channels)) {
        std::cout << "--channel(s) value is not well-formed.\n";
        Usage();
        return;
      }

      for (auto& channel : channels) {
        if (channel.first == 0) {
          if (log_ids.size() == 1) {
            channel =
                std::pair<uint32_t, uint32_t>(log_ids.front(), channel.second);
          } else {
            std::cout << "--channel(s) values must be <log id>.<channel id> "
                         "when multiple logs are viewed.\n";
            Usage();
            return;
          }
        }

        viewer_.EnableChannel(channel);
      }
    }

    if (command_line.GetOptionValue("stop-index", &string_value)) {
      if (log_ids.size() == 0) {
        std::cout << "--stop-index option not applicable.\n";
        Usage();
        return;
      }

      std::pair<uint32_t, uint32_t> stop_index;
      if (!Parse(string_value, &stop_index)) {
        std::cout << "--stop-index value is not well-formed.\n";
        Usage();
        return;
      }

      if (stop_index.first == 0) {
        if (log_ids.size() == 1) {
          stop_index =
              std::pair<uint32_t, uint32_t>(log_ids.front(), stop_index.second);
        } else {
          std::cout << "--stop-index value must be <log id>.<index> when "
                       "multiple logs are viewed\n";
          Usage();
          return;
        }
      }

      viewer_.set_stop_index(stop_index);
    }

    bool did_something = false;

    if (!log_ids.empty()) {
      viewer_.ProcessLogs(log_ids);
      did_something = true;
    }

    if (command_line.HasOption("delete-all-logs")) {
      viewer_.DeleteAllLogs();
      did_something = true;
    } else if (command_line.GetOptionValue("delete-log", &string_value) ||
               command_line.GetOptionValue("delete-logs", &string_value)) {
      std::vector<uint32_t> logs;
      if (!Parse(string_value, &logs)) {
        std::cout << "--delete-log(s) value is not well-formed.\n";
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
    std::cout
        << "\nusage: flog_viewer <args>\n"
        << "    <log ids>              process specified log(s)\n"
        << "    --format=<format>      digest (default), full, or terse\n"
        << "    --channel(s)=<ids>     process only the indicated channels\n"
        << "    --stop-index=<index>   process up to the indicated index\n"
        << "    --delete-log(s)=<ids>  delete the indicated logs\n"
        << "    --delete-all-logs      delete all logs\n"
        << "If no arguments are supplied, a list of logs is displayed.\n"
        << "Lists of values are comma-separated.\n"
        << "If more than one log is to be viewed, channel and stop index must\n"
        << "specify log id, as in <log id>.<channel/index>.\n\n";

    mtl::MessageLoop::GetCurrent()->PostQuitTask();
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
