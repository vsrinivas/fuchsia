// Copyright 2020 The Fuchsia Authors. All rights reserved.  // Use of this source code is governed
// by a BSD-style license that can be // found in the LICENSE file.

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>

#include <examples/diagnostics/inspect/codelab/cpp/testing/integration_test.h>

// [START include_json]
#include <rapidjson/document.h>
#include <rapidjson/pointer.h>
// [END include_json]
#include <src/lib/fsl/vmo/strings.h>

using ContentVector = std::vector<fuchsia::diagnostics::FormattedContent>;

class Part5IntegrationTest : public codelab::testing::IntegrationTest {
 protected:
  // [START get_inspect]
  std::string GetInspectJson() {
    fuchsia::diagnostics::ArchiveAccessorPtr archive;
    auto svc = sys::ServiceDirectory::CreateFromNamespace();
    svc->Connect(archive.NewRequest());

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
        args[0].set_raw_selector(ReverserMonikerForSelectors() + ":root");

        fuchsia::diagnostics::ClientSelectorConfiguration client_selector_config;
        client_selector_config.set_selectors(std::move(args));
        stream_parameters.set_client_selector_configuration(std::move(client_selector_config));
      }
      archive->StreamDiagnostics(std::move(stream_parameters), iterator.NewRequest());

      bool done = false;
      iterator->GetNext([&](auto result) {
        auto res =
            fpromise::result<ContentVector, fuchsia::diagnostics::ReaderError>(std::move(result));
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
};

TEST_F(Part5IntegrationTest, StartWithFizzBuzz) {
  auto ptr = ConnectToReverser({.include_fizzbuzz = true});

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

TEST_F(Part5IntegrationTest, StartWithoutFizzBuzz) {
  auto ptr = ConnectToReverser({.include_fizzbuzz = false});

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
