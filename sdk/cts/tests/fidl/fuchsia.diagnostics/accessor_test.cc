// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/result.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/component_context.h>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <re2/re2.h>
#include <zxtest/zxtest.h>

namespace {

class AccessorTest : public zxtest::Test {};
using namespace component_testing;

const char EXPECTED_DATA[] = R"JSON({
    "data_source": "Inspect",
    "metadata": {
        "component_url": "COMPONENT_URL",
        "filename": "fuchsia.inspect.Tree",
        "timestamp": TIMESTAMP
    },
    "moniker": "realm_builder\\:CHILD_NAME/inspect-publisher",
    "payload": {
        "root": {
            "arrays": {
                "doubles": [
                    0.0,
                    0.0,
                    3.5,
                    0.0
                ],
                "ints": [
                    -1,
                    0
                ],
                "uints": [
                    0,
                    2,
                    0
                ]
            },
            "buffers": {
                "bytes": "b64:AQID",
                "string": "foo"
            },
            "exponential_histograms": {
                "double": {
                    "buckets": [
                        {
                            "ceiling": 1.5,
                            "count": 0.0,
                            "floor": -1.7976931348623157e308
                        },
                        {
                            "ceiling": 3.5,
                            "count": 0.0,
                            "floor": 1.5
                        },
                        {
                            "ceiling": 8.5,
                            "count": 1.0,
                            "floor": 3.5
                        },
                        {
                            "ceiling": 26.0,
                            "count": 0.0,
                            "floor": 8.5
                        },
                        {
                            "ceiling": 1.7976931348623157e308,
                            "count": 0.0,
                            "floor": 26.0
                        }
                    ]
                },
                "int": {
                    "buckets": [
                        {
                            "ceiling": -10,
                            "count": 0,
                            "floor": -9223372036854775808
                        },
                        {
                            "ceiling": -8,
                            "count": 0,
                            "floor": -10
                        },
                        {
                            "ceiling": -4,
                            "count": 1,
                            "floor": -8
                        },
                        {
                            "ceiling": 8,
                            "count": 0,
                            "floor": -4
                        },
                        {
                            "ceiling": 9223372036854775807,
                            "count": 0,
                            "floor": 8
                        }
                    ]
                },
                "uint": {
                    "buckets": [
                        {
                            "ceiling": 1,
                            "count": 0,
                            "floor": 0
                        },
                        {
                            "ceiling": 3,
                            "count": 0,
                            "floor": 1
                        },
                        {
                            "ceiling": 7,
                            "count": 1,
                            "floor": 3
                        },
                        {
                            "ceiling": 19,
                            "count": 0,
                            "floor": 7
                        },
                        {
                            "ceiling": 18446744073709551615,
                            "count": 0,
                            "floor": 19
                        }
                    ]
                }
            },
            "linear_histgorams": {
                "double": {
                    "buckets": [
                        {
                            "ceiling": 1.5,
                            "count": 0.0,
                            "floor": -1.7976931348623157e308
                        },
                        {
                            "ceiling": 4.0,
                            "count": 0.0,
                            "floor": 1.5
                        },
                        {
                            "ceiling": 6.5,
                            "count": 1.0,
                            "floor": 4.0
                        },
                        {
                            "ceiling": 9.0,
                            "count": 0.0,
                            "floor": 6.5
                        },
                        {
                            "ceiling": 1.7976931348623157e308,
                            "count": 0.0,
                            "floor": 9.0
                        }
                    ]
                },
                "int": {
                    "buckets": [
                        {
                            "ceiling": -10,
                            "count": 0,
                            "floor": -9223372036854775808
                        },
                        {
                            "ceiling": -8,
                            "count": 0,
                            "floor": -10
                        },
                        {
                            "ceiling": -6,
                            "count": 0,
                            "floor": -8
                        },
                        {
                            "ceiling": -4,
                            "count": 1,
                            "floor": -6
                        },
                        {
                            "ceiling": 9223372036854775807,
                            "count": 0,
                            "floor": -4
                        }
                    ]
                },
                "uint": {
                    "buckets": [
                        {
                            "ceiling": 1,
                            "count": 0,
                            "floor": 0
                        },
                        {
                            "ceiling": 3,
                            "count": 0,
                            "floor": 1
                        },
                        {
                            "ceiling": 5,
                            "count": 1,
                            "floor": 3
                        },
                        {
                            "ceiling": 7,
                            "count": 0,
                            "floor": 5
                        },
                        {
                            "ceiling": 18446744073709551615,
                            "count": 0,
                            "floor": 7
                        }
                    ]
                }
            },
            "numeric": {
                "bool": true,
                "double": 1.5,
                "int": -1,
                "uint": 1
            }
        }
    },
    "version": 1
})JSON";

// Tests that reading inspect data returns expected data from the archive accessor.
TEST_F(AccessorTest, StreamDiagnosticsInspect) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  static constexpr char kInspectPublisher[] = "inspect-publisher";
  static constexpr auto kInspectPublisherUrl = "#meta/inspect-publisher.cm";

  auto context = sys::ComponentContext::Create();

  fuchsia::diagnostics::ArchiveAccessorPtr accessor;
  ASSERT_EQ(ZX_OK, context->svc()->Connect(accessor.NewRequest()));

  auto realm = RealmBuilder::Create(context->svc())
                   .AddChild(kInspectPublisher, kInspectPublisherUrl,
                             ChildOptions{.startup_mode = StartupMode::EAGER})
                   .AddRoute(Route{
                       .capabilities = {Protocol{"fuchsia.logger.LogSink"}},
                       .source = ParentRef(),
                       .targets = {ChildRef{kInspectPublisher}},
                   })
                   .Build(loop.dispatcher());

  auto _binder = realm.ConnectSync<fuchsia::component::Binder>();

  auto selector = "realm_builder\\:" + realm.GetChildName() + "/inspect-publisher:root";
  inspect::contrib::ArchiveReader reader(std::move(accessor), {selector});

  fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string> actual_result;
  fpromise::single_threaded_executor executor;

  executor.schedule_task(
      reader.SnapshotInspectUntilPresent({"inspect-publisher"})
          .then([&](fpromise::result<std::vector<inspect::contrib::DiagnosticsData>, std::string>&
                        result) mutable { actual_result = std::move(result); }));

  executor.run();

  EXPECT_TRUE(actual_result.is_ok());

  auto& data = actual_result.value()[0];
  data.Sort();
  std::string actual = data.PrettyJson();
  re2::RE2::GlobalReplace(&actual, re2::RE2("\"component_url\": \".+\""),
                          "\"component_url\": \"COMPONENT_URL\"");
  re2::RE2::GlobalReplace(&actual, re2::RE2("        \"errors\": null,\n"), "");

  std::string timestamp;
  EXPECT_TRUE(re2::RE2::PartialMatch(actual, re2::RE2("\"timestamp\": (\\d+)"), &timestamp));

  std::string expected = EXPECTED_DATA;

  // Replace non-deterministic expected values.
  re2::RE2::GlobalReplace(&expected, re2::RE2("CHILD_NAME"), realm.GetChildName());
  re2::RE2::GlobalReplace(&expected, re2::RE2("TIMESTAMP"), timestamp);

  EXPECT_EQ(expected, actual);
}

}  // namespace
