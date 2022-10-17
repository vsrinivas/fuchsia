// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples.calculator/cpp/fidl.h>
// Note: the pattern for the generated Natural bindings is:
// #include <fidl/<my.library.name>/cpp/fidl.h>
// For more information on the include path to the bindings, refer to:
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp/basics/domain-objects#include-cpp-bindings

#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>

// The parser library to handle an input file for the Calcutator
#include "examples/fidl/calculator/cpp/client/calc_parser.h"

// A Calculator Client class to help structure the code and make it more realistic. Could be easily
// extended to handle FIDL events by overriding fidl::AsyncEventHandler<>
class CalculatorClient {
 public:
  // Construct the Client with the loop dispatcher
  explicit CalculatorClient(
      async::Loop& loop, fidl::ClientEnd<fuchsia_examples_calculator::Calculator> client_endpoint)
      : expected_responses_(0), loop_(loop) {
    // We need to call Bind() since the fidl::Client is uninitialized
    fidl_client_.Bind(std::move(client_endpoint), loop_.dispatcher());
    // Check if the fidl::Client was initialized properly
    if (!fidl_client_.is_valid()) {
      FX_LOGS(ERROR) << "Client could not be bound!";
    }
  }

  // A helper function to check for validity before making client calls
  bool IsValid() { return fidl_client_.is_valid(); }

  void Add(double left, double right) {
    // Make the FIDL method call by dereferencing the |fidl_client| and registering the response
    // callback.
    // For more information about .ThenExactlyOnce(), please see:
    // https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp?#motivation_for_then
    fidl_client_->Add({{.a = left, .b = right}})
        .ThenExactlyOnce([&](fidl::Result<fuchsia_examples_calculator::Calculator::Add>& result) {
          if (!result.is_ok()) {
            FX_LOGS(ERROR) << "Calculator client: " << __func__ << "(): "
                           << "Failure receiving response: " << result.error_value();
          }
          FX_LOGS(INFO) << "Calculator client: " << __func__ << "(): "
                        << " got response " << result->sum();
          ReceivedResponseMaybeQuit();
        });
  }

  void Subtract(double left, double right) {
    // Make the FIDL method call by dereferencing the |fidl_client| and registering the response
    // callback.
    fidl_client_->Subtract({{.a = left, .b = right}})
        .ThenExactlyOnce(
            [&](fidl::Result<fuchsia_examples_calculator::Calculator::Subtract>& result) {
              if (!result.is_ok()) {
                FX_LOGS(ERROR) << "Calculator client: " << __func__ << "(): "
                               << "Failure receiving response: " << result.error_value();
              }
              FX_LOGS(INFO) << "Calculator client: " << __func__ << "(): "
                            << " got response " << result->difference();
              ReceivedResponseMaybeQuit();
            });
  }

  void Multiply(double left, double right) {
    // Make the FIDL method call by dereferencing the |fidl_client| and registering the response
    // callback.
    fidl_client_->Multiply({{.a = left, .b = right}})
        .ThenExactlyOnce(
            [&](fidl::Result<fuchsia_examples_calculator::Calculator::Multiply>& result) {
              if (!result.is_ok()) {
                FX_LOGS(ERROR) << "Calculator client: " << __func__ << "(): "
                               << "Failure receiving response: " << result.error_value();
              }
              FX_LOGS(INFO) << "Calculator client: " << __func__ << "(): "
                            << " got response " << result->product();
              ReceivedResponseMaybeQuit();
            });
  }

  void Divide(double left, double right) {
    // Make the FIDL method call by dereferencing the |fidl_client| and registering the response
    // callback.
    fidl_client_->Divide({{.dividend = left, .divisor = right}})
        .ThenExactlyOnce(
            [&](fidl::Result<fuchsia_examples_calculator::Calculator::Divide>& result) {
              if (!result.is_ok()) {
                FX_LOGS(ERROR) << "Calculator client: " << __func__ << "(): "
                               << "Failure receiving response: " << result.error_value();
              }
              FX_LOGS(INFO) << "Calculator client: " << __func__ << "(): "
                            << " got response " << result->quotient();
              ReceivedResponseMaybeQuit();
            });
  }

  void Pow(double left, double right) {
    // Make the FIDL method call by dereferencing the |fidl_client| and registering the response
    // callback.
    fidl_client_->Pow({{.base = left, .exponent = right}})
        .ThenExactlyOnce([&](fidl::Result<fuchsia_examples_calculator::Calculator::Pow>& result) {
          if (!result.is_ok()) {
            FX_LOGS(ERROR) << "Calculator client: " << __func__ << "(): "
                           << "Failure receiving response: " << result.error_value();
          }
          FX_LOGS(INFO) << "Calculator client: " << __func__ << "(): "
                        << " got response " << result->power();
          ReceivedResponseMaybeQuit();
        });
  }

  void MakeClientRequest(calc::Expression& expression) {
    // Increment the counter since we're about to make a request
    expected_responses_++;
    // Call the appropriate handler method
    switch (expression.GetOperator()) {
      case calc::Operator::Add:
        Add(expression.GetLeft(), expression.GetRight());
        break;
      case calc::Operator::Subtract:
        Subtract(expression.GetLeft(), expression.GetRight());
        break;
      case calc::Operator::Multiply:
        Multiply(expression.GetLeft(), expression.GetRight());
        break;
      case calc::Operator::Divide:
        Divide(expression.GetLeft(), expression.GetRight());
        break;
      case calc::Operator::Pow:
        Pow(expression.GetLeft(), expression.GetRight());
        break;
      default:
        FX_LOGS(ERROR) << "Not implemented on client";
        // Since we didn't actually send a request, decrement the counter
        expected_responses_--;
    }
  }

  // A helper function so this async client shuts itself down after receiving all responses
  void ReceivedResponseMaybeQuit() {
    expected_responses_--;
    if (expected_responses_ <= 0) {
      FX_LOGS(INFO) << "Received all responses, shutting down client";
      loop_.Quit();
    }
  }

 private:
  // A counter so we can shut down the async client loop when all responses have been received
  size_t expected_responses_;
  // Since this is an async client, it needs a loop dispatcher
  async::Loop& loop_;
  // The |fidl::Client| handle we use to make FIDL method calls
  fidl::Client<fuchsia_examples_calculator::Calculator> fidl_client_;
};

int main(int argc, const char** argv) {
  syslog::SetTags({"calculator_client"});

  // Note the path starts with /pkg/ even though the build rule
  // `resource("input")` uses `data/input.txt`. At runtime, components are
  // able to read the contents of their own package by accessing the path
  // /pkg/ in their namespace. See
  // https://fuchsia.dev/fuchsia-src/development/components/data#including_resources_with_a_component
  // for more details.
  const std::string input_filename = "/pkg/data/input.txt";
  std::ifstream myfile(input_filename);

  if (!myfile.is_open()) {
    FX_LOGS(ERROR) << "Failed opening the input file: '" << input_filename << "'";
    // TODO(fxbug.dev/96667): Remove sleep once the logging bug is fixed.
    sleep(1);
    return 0;
  }

  // Parse file contents into memory
  std::vector<std::string> file_contents;
  while (myfile) {
    std::string current_line;
    std::getline(myfile, current_line);
    file_contents.push_back(current_line);
  }

  // Connect to the fuchsia.examples.calculator/Calculator protocol. This can fail so it's wrapped
  // in a |zx::result| and it must be checked for errors.
  // Note: The actual success type type is |fidl::ClientEnd|, which is used to create the
  // |fidl::Client|
  zx::result client_end = component::Connect<fuchsia_examples_calculator::Calculator>();
  if (!client_end.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the |Calculator| protocol: "
                   << client_end.status_string();
    return -1;
  }

  // As in the server, the code sets up an async loop so that the client can
  // listen for incoming responses from the server without blocking.
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  // Create the Client instance
  CalculatorClient client_instance(loop, std::move(*client_end));

  // Check if the client initialized correctly
  if (!client_instance.IsValid()) {
    // TODO(fxbug.dev/96667): Remove sleep once the logging bug is fixed.
    sleep(1);
    return -1;
  }

  // Send the parsed commands as FIDL requests to the Calculator server
  for (auto current_cmd_line : file_contents) {
    // Create the Expression
    calc::Expression current_expression(current_cmd_line);
    // Check to make sure we have a valid Expression
    if (current_expression.GetOperator() == calc::Operator::PlaceHolderError) {
      FX_LOGS(WARNING) << "Invalid input to calculator client, skipping: '" << current_cmd_line
                       << "'";
      // Skip this, as we don't have a valid Expression
      continue;
    }
    // Call the appropriate client method
    client_instance.MakeClientRequest(current_expression);
  }
  // Run() the async loop dispatcher to handle callbacks
  loop.Run();
  // TODO(fxbug.dev/96667): Remove sleep once the logging bug is fixed.
  sleep(1);
  return 0;
}
