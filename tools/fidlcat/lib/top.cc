// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/top.h"

#include <vector>

#include "tools/fidlcat/lib/event.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

void Top::Display(std::ostream& os) {
  std::vector<Process*> sorted_processes;
  for (const auto& process : dispatcher_->processes()) {
    sorted_processes.emplace_back(process.second.get());
  }
  std::sort(sorted_processes.begin(), sorted_processes.end(), [](Process* left, Process* right) {
    return left->event_count() > right->event_count();
  });

  const char* separator = "";
  // Displays all the processes one after the other.
  for (const auto& process : sorted_processes) {
    FidlcatPrinter printer(dispatcher_, process, os,
                           dispatcher_->extra_generation_needs_colors() ? fidl_codec::WithColors
                                                                        : fidl_codec::WithoutColors,
                           "");
    printer << separator;
    for (int i = 0; i < dispatcher_->columns(); ++i) {
      printer << '-';
    }
    printer << *process << ": " << process->event_count()
            << ((process->event_count() == 1) ? " event" : " events") << '\n';
    fidl_codec::Indent indent(printer);
    DisplayProcessContent(printer, process);
    separator = "\n";
  }
}

void Top::DisplayProcessContent(FidlcatPrinter& printer, Process* process) {
  std::vector<Protocol*> sorted_protocols;
  for (const auto& protocol : process->protocols()) {
    sorted_protocols.emplace_back(protocol.second.get());
  }
  std::sort(sorted_protocols.begin(), sorted_protocols.end(), [](Protocol* left, Protocol* right) {
    return (left->event_count() > right->event_count()) ||
           ((left->event_count() == right->event_count()) &&
            ((left->interface() == nullptr) || (right->interface() == nullptr) ||
             (left->interface()->name().compare(right->interface()->name()) < 0)));
  });

  const char* separator = "";
  // Displays all the protocols one after the other.
  for (const auto& protocol : sorted_protocols) {
    printer << separator;
    if (protocol->interface() == nullptr) {
      printer << "unknown interfaces: "
              << ": " << protocol->event_count()
              << ((protocol->event_count() == 1) ? " event" : " events") << '\n';
    } else {
      printer << protocol->interface()->name() << ": " << protocol->event_count()
              << ((protocol->event_count() == 1) ? " event" : " events") << '\n';
    }
    fidl_codec::Indent indent(printer);
    DisplayProtocolContent(printer, protocol);
    separator = "\n";
  }
}

void Top::DisplayProtocolContent(FidlcatPrinter& printer, Protocol* protocol) {
  std::vector<Method*> sorted_methods;
  for (const auto& method : protocol->methods()) {
    sorted_methods.emplace_back(method.second.get());
  }
  std::sort(sorted_methods.begin(), sorted_methods.end(), [](Method* left, Method* right) {
    return (left->event_count() > right->event_count()) ||
           ((left->event_count() == right->event_count()) &&
            ((left->method() == nullptr) || (right->method() == nullptr) ||
             (left->method()->name().compare(right->method()->name()) < 0)));
  });

  // Displays all the methods one after the other.
  for (const auto& method : sorted_methods) {
    if (method->method() != nullptr) {
      printer << method->method()->name() << ": " << method->event_count()
              << ((method->event_count() == 1) ? " event" : " events") << '\n';
    }
    fidl_codec::Indent indent(printer);
    for (const auto event : method->events()) {
      event->Display(printer, /*with_channel=*/true);
    }
  }
}

}  // namespace fidlcat
