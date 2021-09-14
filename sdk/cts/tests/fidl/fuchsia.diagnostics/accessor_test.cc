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
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/realm_builder.h>
#include <lib/sys/cpp/testing/realm_builder_types.h>

#include <regex>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <zxtest/zxtest.h>

namespace {

class AccessorTest : public zxtest::Test {};
using namespace sys::testing;

const char EXPECTED_DATA[] = R"JSON({
    "data_source": "Inspect",
    "metadata": {
        "component_url": "COMPONENT_URL",
        "errors": null,
        "filename": "fuchsia.inspect.Tree",
        "timestamp": TIMESTAMP
    },
    "moniker": "fuchsia_component_test_collection\\:CHILD_NAME/inspect-publisher",
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

  static constexpr auto kInspectPublisher = Moniker{"inspect-publisher"};
  static constexpr auto kInspectPublisherUrl = "#meta/inspect-publisher.cm";

  auto context = sys::ComponentContext::Create();

  fuchsia::diagnostics::ArchiveAccessorPtr accessor;
  ASSERT_EQ(ZX_OK, context->svc()->Connect(accessor.NewRequest()));

  auto realm = Realm::Builder::New(context.get())
                   .AddComponent(kInspectPublisher,
                                 Component{
                                     .source = ComponentUrl{kInspectPublisherUrl},
                                     .eager = true,
                                 })
                   .AddRoute(CapabilityRoute{
                       .capability = Protocol{"fuchsia.logger.LogSink"},
                       .source = AboveRoot(),
                       .targets = {kInspectPublisher},
                   })
                   .Build(loop.dispatcher());

  auto _binder = realm.ConnectSync<fuchsia::component::Binder>();

  auto selector =
      "fuchsia_component_test_collection\\:" + realm.GetChildName() + "/inspect-publisher:root";
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
  actual = std::regex_replace(actual, std::regex("\"component_url\": \".+\""),
                              "\"component_url\": \"COMPONENT_URL\"");

  std::smatch timestamp_m;
  EXPECT_TRUE(std::regex_search(actual, timestamp_m, std::regex("\"timestamp\": (\\d+)")));

  std::string expected = EXPECTED_DATA;

  // Replace non-deterministic expected values.
  expected = std::regex_replace(expected, std::regex("CHILD_NAME"), realm.GetChildName());
  expected = std::regex_replace(expected, std::regex("TIMESTAMP"), timestamp_m.str(1));

  EXPECT_EQ(expected, actual);
}

}  // namespace
