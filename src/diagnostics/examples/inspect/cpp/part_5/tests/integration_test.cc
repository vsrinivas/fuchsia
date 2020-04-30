// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/examples/inspect/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

// [START include_json]
#include <rapidjson/document.h>
#include <rapidjson/pointer.h>
// [END include_json]
#include <src/lib/fsl/vmo/strings.h>

constexpr char reverser_url[] =
    "fuchsia-pkg://fuchsia.com/inspect_cpp_codelab_integration_tests#meta/"
    "inspect_cpp_codelab_part_5.cmx";

constexpr char fizzbuzz_url[] =
    "fuchsia-pkg://fuchsia.com/inspect_cpp_codelab_integration_tests#meta/"
    "inspect_cpp_codelab_fizzbuzz.cmx";

class CodelabTest : public sys::testing::TestWithEnvironment {
 protected:
  // Options for each test.
  struct TestOptions {
    // If true, inject a FizzBuzz service implementation.
    bool include_fizzbuzz_service;
  };

  fuchsia::examples::inspect::ReverserPtr StartComponentAndConnect(TestOptions options) {
    // Create an environment for the test that simulates the "sys" realm.
    // We optionally inject the "FizzBuzz" service if requested.
    auto services = CreateServices();
    if (options.include_fizzbuzz_service) {
      services->AddServiceWithLaunchInfo({.url = fizzbuzz_url},
                                         fuchsia::examples::inspect::FizzBuzz::Name_);
    }
    environment_ = CreateNewEnclosingEnvironment("sys", std::move(services));

    // Start the Reverser component in the nested environment.
    fuchsia::io::DirectoryPtr directory_request;
    controller_ = environment_->CreateComponent(
        {.url = reverser_url, .directory_request = directory_request.NewRequest().TakeChannel()});

    // Connect to Reverser hosted by the new component.
    fuchsia::examples::inspect::ReverserPtr ret;
    sys::ServiceDirectory component_services(directory_request.Unbind());
    component_services.Connect(ret.NewRequest());

    bool ready = false;
    controller_.events().OnDirectoryReady = [&] { ready = true; };
    RunLoopUntil([&] { return ready; });

    return ret;
  }

  using ContentVector = std::vector<fuchsia::diagnostics::FormattedContent>;

  // [START get_inspect]
  std::string GetInspectJson() {
    fuchsia::diagnostics::ArchiveAccessorPtr archive;
    real_services()->Connect(archive.NewRequest());

    while (true) {
      ContentVector current_entries;

      fuchsia::diagnostics::BatchIteratorPtr iterator;
      fuchsia::diagnostics::StreamParameters stream_parameters;
      stream_parameters.set_data_type(fuchsia::diagnostics::DataType::INSPECT);
      stream_parameters.set_stream_mode(fuchsia::diagnostics::StreamMode::SNAPSHOT);
      stream_parameters.set_format(fuchsia::diagnostics::Format::JSON);

      {
        std::vector<fuchsia::diagnostics::SelectorArgument> args;
        args.emplace_back();
        args[0].set_raw_selector("sys/inspect_cpp_codelab_part_5.cmx:root");

        fuchsia::diagnostics::ClientSelectorConfiguration client_selector_config;
        client_selector_config.set_selectors(std::move(args));
        stream_parameters.set_client_selector_configuration(std::move(client_selector_config));
      }
      archive->StreamDiagnostics(std::move(stream_parameters), iterator.NewRequest());

      bool done = false;
      iterator->GetNext([&](auto result) {
        auto res = fit::result<ContentVector, fuchsia::diagnostics::ReaderError>(std::move(result));
        if (res.is_ok()) {
          current_entries = res.take_value();
        }

        done = true;
      });

      RunLoopUntil([&] { return done; });

      // Should be at most one component.
      ZX_ASSERT(current_entries.size() <= 1);
      if (!current_entries.empty()) {
        std::string json;
        fsl::StringFromVmo(current_entries[0].json(), &json);
        // Ensure the component is either OK or UNHEALTHY.
        if (json.find("OK") != std::string::npos || json.find("UNHEALTHY") != std::string::npos) {
          return json;
        }
      }

      // Retry with delay until the data appears.
      usleep(150000);
    }

    return "";
  }
  // [END get_inspect]

 private:
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  fuchsia::sys::ComponentControllerPtr controller_;
};

TEST_F(CodelabTest, StartWithFizzBuzz) {
  auto ptr = StartComponentAndConnect({.include_fizzbuzz_service = true});

  bool error = false;
  ptr.set_error_handler([&](zx_status_t unused) { error = true; });

  bool done = false;
  std::string result;
  ptr->Reverse("hello", [&](std::string value) {
    result = std::move(value);
    done = true;
  });
  RunLoopUntil([&] { return done || error; });

  ASSERT_FALSE(error);
  EXPECT_EQ("olleh", result);

  // [START parse_result]
  rapidjson::Document document;
  document.Parse(GetInspectJson());
  // [END parse_result]

  EXPECT_EQ(rapidjson::Value("OK"),
            // [START hint_get_value]
            rapidjson::GetValueByPointerWithDefault(
                document, "/payload/root/fuchsia.inspect.Health/status", "")
            // [END hint_get_value]
  );
}

TEST_F(CodelabTest, StartWithoutFizzBuzz) {
  auto ptr = StartComponentAndConnect({.include_fizzbuzz_service = false});

  bool error = false;
  ptr.set_error_handler([&](zx_status_t unused) { error = true; });

  bool done = false;
  std::string result;
  ptr->Reverse("hello", [&](std::string value) {
    result = std::move(value);
    done = true;
  });
  RunLoopUntil([&] { return done || error; });

  ASSERT_FALSE(error);
  EXPECT_EQ("olleh", result);

  rapidjson::Document document;
  document.Parse(GetInspectJson());

  EXPECT_EQ(rapidjson::Value("UNHEALTHY"),
            rapidjson::GetValueByPointerWithDefault(
                document, "/payload/root/fuchsia.inspect.Health/status", ""));
}
