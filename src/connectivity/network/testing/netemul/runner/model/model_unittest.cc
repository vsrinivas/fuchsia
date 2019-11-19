// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "config.h"
#include "gtest/gtest.h"

namespace netemul {
namespace config {
namespace testing {

class ModelTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const char* json, const char* msg) {
    Config config;
    json::JSONParser parser;
    auto doc = parser.ParseFromString(
        json, ::testing::UnitTest::GetInstance()->current_test_info()->name());

    ASSERT_FALSE(parser.HasError());

    EXPECT_FALSE(config.ParseFromJSON(doc, &parser)) << msg;
    ASSERT_TRUE(parser.HasError());
    std::cout << "Parse failed as expected: " << parser.error_str() << std::endl;
  }

  void ExpectSuccessfulParse(const char* json, Config* config) {
    json::JSONParser parser;
    auto doc = parser.ParseFromString(
        json, ::testing::UnitTest::GetInstance()->current_test_info()->name());

    ASSERT_FALSE(parser.HasError());
    ASSERT_TRUE(config->ParseFromJSON(doc, &parser));
    ASSERT_FALSE(parser.HasError());
  }
};

TEST_F(ModelTest, ParseTest) {
  const char* json =
      R"(
    {
      "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/default.cmx",
      "guest": [
        {
          "label": "test_guest",
          "url": "fuchsia-pkg://fuchsia.com/test_guest#meta/test_guest.cmx",
          "networks": ["test-net"],
          "files": {
            "/pkg/data/test.sh": "/root/test_copy.sh"
          },
          "macs": {
            "01:02:03:04:05:06": "test-net"
          }
        }
      ],
      "environment": {
        "children": [
          {
            "name": "child-1",
            "test": [
              {
                "arguments": [
                  "-t",
                  "1",
                  "-n",
                  "child-1-url"
                ],
                "url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/env_build_run.cmx"
              },
              {
                "arguments": [
                  "-t",
                  "1",
                  "-n",
                  "child-1-no-url"
                ]
              }
            ],
            "logger_options": {
              "enabled": false,
              "filters": null
            }
          },
          {
            "inherit_services": false,
            "name": "child-2",
            "test": [ "fuchsia-pkg://fuchsia.com/some_test#meta/some_test.cmx" ],
            "apps" : [ "fuchsia-pkg://fuchsia.com/some_app#meta/some_app.cmx" ],
            "logger_options": {
              "enabled": true,
              "klogs_enabled": true,
              "filters": {
                "verbosity": 1,
                "tags": ["testtag"]
              }
            }
          }
        ],
        "devices": [
          "ep0",
          "ep1"
        ],
        "name": "root",
        "setup": [
          {
            "url": "fuchsia-pkg://fuchsia.com/some_setup#meta/some_setup.cmx",
            "arguments": ["-arg"]
          }
        ],
        "services": {
          "fuchsia.netstack.Netstack": "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx",
          "fuchsia.some.Service" : {
            "url" : "fuchsia-pkg://fuchsia.com/some_service#meta/some_service.cmx",
            "arguments" : ["-a1", "-a2"]
          }
        }
      },
      "networks": [
        {
          "endpoints": [
            {
              "mac": "70:00:01:02:03:04",
              "mtu": 1000,
              "name": "ep0",
              "up": false
            },
            {
              "name": "ep1"
            }
          ],
          "name": "test-net"
        }
      ]
    }
)";

  config::Config config;
  json::JSONParser parser;
  auto doc = parser.ParseFromString(json, "ParseTest");
  EXPECT_FALSE(parser.HasError()) << "Parse error: " << parser.error_str();

  EXPECT_TRUE(config.ParseFromJSON(doc, &parser)) << "Parse error: " << parser.error_str();

  EXPECT_EQ(config.default_url(),
            "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/default.cmx");
  EXPECT_EQ(config.disabled(), false);
  EXPECT_EQ(config.timeout(), zx::duration::infinite());
  EXPECT_EQ(config.capture(), CaptureMode::NONE);

  // sanity check the objects:
  auto& root_env = config.environment();
  EXPECT_EQ(root_env.name(), "root");
  EXPECT_EQ(root_env.inherit_services(), true);
  EXPECT_EQ(root_env.children().size(), 2ul);
  EXPECT_EQ(root_env.devices().size(), 2ul);
  EXPECT_EQ(root_env.services().size(), 2ul);
  EXPECT_TRUE(root_env.test().empty());
  EXPECT_TRUE(root_env.apps().empty());
  EXPECT_EQ(root_env.setup().size(), 1ul);

  // check the guests
  EXPECT_EQ(config.guests().size(), 1ul);
  EXPECT_EQ(config.guests()[0].guest_label(), "test_guest");
  EXPECT_EQ(config.guests()[0].guest_image_url(),
            "fuchsia-pkg://fuchsia.com/test_guest#meta/test_guest.cmx");
  EXPECT_EQ(config.guests()[0].networks().size(), 1ul);
  EXPECT_EQ(config.guests()[0].files().size(), 1ul);
  EXPECT_EQ(config.guests()[0].macs().size(), 1ul);

  // check the devices
  EXPECT_EQ(root_env.devices()[0], "ep0");
  EXPECT_EQ(root_env.devices()[1], "ep1");

  // check the services
  EXPECT_EQ(root_env.services()[0].launch().url(),
            "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx");
  EXPECT_TRUE(root_env.services()[0].launch().arguments().empty());
  EXPECT_EQ(root_env.services()[0].name(), "fuchsia.netstack.Netstack");
  EXPECT_EQ(root_env.services()[0].launch().url(),
            "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx");
  EXPECT_TRUE(root_env.services()[0].launch().arguments().empty());
  EXPECT_EQ(root_env.services()[1].name(), "fuchsia.some.Service");
  EXPECT_EQ(root_env.services()[1].launch().url(),
            "fuchsia-pkg://fuchsia.com/some_service#meta/some_service.cmx");
  EXPECT_EQ(root_env.services()[1].launch().arguments().size(), 2ul);

  // check the logger_options
  EXPECT_TRUE(root_env.logger_options().enabled());
  EXPECT_FALSE(root_env.logger_options().klogs_enabled());
  EXPECT_EQ(root_env.logger_options().filters().verbosity(), 0);
  EXPECT_TRUE(root_env.logger_options().filters().tags().empty());

  // check the child environments
  auto& c0 = root_env.children()[0];
  EXPECT_EQ(c0.name(), "child-1");
  EXPECT_EQ(c0.inherit_services(), true);
  EXPECT_TRUE(c0.children().empty());
  EXPECT_TRUE(c0.devices().empty());
  EXPECT_TRUE(c0.services().empty());
  EXPECT_EQ(c0.test().size(), 2ul);
  EXPECT_TRUE(c0.apps().empty());
  EXPECT_TRUE(c0.setup().empty());
  auto& c1 = root_env.children()[1];
  EXPECT_EQ(c1.name(), "child-2");
  EXPECT_EQ(c1.inherit_services(), false);
  EXPECT_TRUE(c1.children().empty());
  EXPECT_TRUE(c1.devices().empty());
  EXPECT_TRUE(c1.services().empty());
  EXPECT_TRUE(c1.setup().empty());
  EXPECT_EQ(c1.test().size(), 1ul);
  EXPECT_EQ(c1.apps().size(), 1ul);

  // check test structures:
  auto& t0 = c0.test()[0];
  auto& t1 = c0.test()[1];
  auto& t2 = c1.test()[0];
  EXPECT_EQ(t0.url(), "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/env_build_run.cmx");
  EXPECT_EQ(t0.arguments().size(), 4ul);

  EXPECT_TRUE(t1.url().empty());
  EXPECT_EQ(t1.arguments().size(), 4ul);
  EXPECT_EQ(t2.url(), "fuchsia-pkg://fuchsia.com/some_test#meta/some_test.cmx");
  EXPECT_TRUE(t2.arguments().empty());

  // check apps:
  auto& app0 = c1.apps()[0];
  EXPECT_EQ(app0.url(), "fuchsia-pkg://fuchsia.com/some_app#meta/some_app.cmx");
  EXPECT_TRUE(app0.arguments().empty());

  // check setup:
  auto& setup = root_env.setup()[0];
  EXPECT_EQ(setup.url(), "fuchsia-pkg://fuchsia.com/some_setup#meta/some_setup.cmx");
  EXPECT_EQ(setup.arguments().size(), 1ul);
  EXPECT_EQ(setup.arguments()[0], "-arg");

  // check network object:
  EXPECT_EQ(config.networks().size(), 1ul);
  auto& net = config.networks()[0];
  EXPECT_EQ(net.name(), "test-net");
  EXPECT_EQ(net.endpoints().size(), 2ul);

  // check endpoints:
  auto& ep0 = net.endpoints()[0];
  auto& ep1 = net.endpoints()[1];
  EXPECT_EQ(ep0.name(), "ep0");
  EXPECT_EQ(ep0.mtu(), 1000u);
  EXPECT_TRUE(ep0.mac());
  const uint8_t mac_cmp[] = {0x70, 0x00, 0x01, 0x02, 0x03, 0x04};
  EXPECT_EQ(memcmp(mac_cmp, ep0.mac()->d, sizeof(mac_cmp)), 0);
  EXPECT_EQ(ep0.up(), false);

  EXPECT_EQ(ep1.name(), "ep1");
  EXPECT_EQ(ep1.mtu(), 1500u);  // default mtu check
  EXPECT_FALSE(ep1.mac());      // mac not set
  EXPECT_EQ(ep1.up(), true);    // default up

  // check logger options
  EXPECT_FALSE(c0.logger_options().enabled());
  EXPECT_FALSE(c0.logger_options().klogs_enabled());
  EXPECT_EQ(c0.logger_options().filters().verbosity(), 0);
  EXPECT_TRUE(c0.logger_options().filters().tags().empty());

  EXPECT_TRUE(c1.logger_options().enabled());
  EXPECT_TRUE(c1.logger_options().klogs_enabled());
  EXPECT_EQ(c1.logger_options().filters().verbosity(), 1);
  EXPECT_EQ(c1.logger_options().filters().tags().size(), 1lu);
  EXPECT_EQ(c1.logger_options().filters().tags()[0], "testtag");
}

TEST_F(ModelTest, NetworkNoName) {
  const char* json = R"({"networks":[{}]})";
  ExpectFailedParse(json, "network without name accepted");

  const char* json2 = R"({"networks":[{"name":""}]})";
  ExpectFailedParse(json2, "network with empty name accepted");
};

TEST_F(ModelTest, EndpointNoName) {
  const char* json = R"({"networks":[{"name":"net","endpoints":[{}]}]})";
  ExpectFailedParse(json, "endpoint without name accepted");

  const char* json2 = R"({"networks":[{"name":"net","endpoints":[{"name":""}]}]})";
  ExpectFailedParse(json2, "endpoint with empty name accepted");
};

TEST_F(ModelTest, EndpointBadMtu) {
  const char* json = R"({"networks":[{"name":"net","endpoints":[{"name":"a","mtu":0}]}]})";
  ExpectFailedParse(json, "endpoint without 0 mtu accepted");
}

TEST_F(ModelTest, EndpointBadMac) {
  const char* json = R"({"networks":[{"name":"net","endpoints":[{"name":"a","mac":"xx:xx:xx"}]}]})";
  ExpectFailedParse(json, "endpoint with invalid mac accepted");
}

TEST_F(ModelTest, TestBadUrl) {
  const char* json = R"({"environment":{"test":[{"url":"blablabla"}]}})";
  ExpectFailedParse(json, "test with bad url accepted");
}

TEST_F(ModelTest, TestBadLoggerOptions) {
  const char* json = R"({"environment":{ "logger_options": [] }})";
  ExpectFailedParse(json, "test with non object for logger_options accepted");

  json = R"({"environment":{ "logger_options": {"enabled": 0 } }})";
  ExpectFailedParse(json, "test with non boolean for logger_options.enabled accepted");

  json = R"({"environment":{ "logger_options": {"klogs_enabled": 0 } }})";
  ExpectFailedParse(json, "test with non boolean for logger_options.klogs_enabled accepted");
}

TEST_F(ModelTest, TestNullLoggerOptions) {
  const char* json = R"({"environment": {"logger_options": null}})";

  config::Config config;
  json::JSONParser parser;
  auto doc = parser.ParseFromString(json, "ParseTest");
  EXPECT_FALSE(parser.HasError()) << "Parse error: " << parser.error_str();

  EXPECT_TRUE(config.ParseFromJSON(doc, &parser)) << "Parse error: " << parser.error_str();

  auto& root_env = config.environment();

  // check the logger_options defaults with null is supplied to logger_options
  EXPECT_EQ(root_env.logger_options().enabled(), true);
  EXPECT_EQ(root_env.logger_options().klogs_enabled(), false);
}

TEST_F(ModelTest, TestBadLoggerFilterOptions) {
  const char* json = R"({"environment": {"logger_options": {"filters": []}}})";
  ExpectFailedParse(json, "test with non object for logger_options.filters accepted");

  json = R"({"environment": {"logger_options": {"filters": {"verbosity": true}}}})";
  ExpectFailedParse(json, "test with non uint for logger_options.filters.verbosity");

  json = R"({"environment": {"logger_options": {"filters": {"tags": {}}}}})";
  ExpectFailedParse(json, "test with non array for logger_options.filters.tags");

  json =
      R"({"environment": {"logger_options": {"filters": {"tags": ["a", "b", "c", "d", "e", "f"]}}}})";
  ExpectFailedParse(json, "test with too many tags for logger_options.filters.tags");

  json =
      R"({"environment": {"logger_options": {"filters": {"tags": ["sdfkjaskhfgaskjfhASFSADFSAFSADFsdfkjaskhfgaskjfhASFSADFSAFSADFDD"]}}}})";
  ExpectFailedParse(json, "test with too long of a tag for logger_options.filters.tags");
}

TEST_F(ModelTest, TestNullLoggerFilterOptions) {
  const char* json = R"({"environment": {"logger_options": {"filters": null}}})";

  config::Config config;
  json::JSONParser parser;
  auto doc = parser.ParseFromString(json, "ParseTest");
  EXPECT_FALSE(parser.HasError()) << "Parse error: " << parser.error_str();

  EXPECT_TRUE(config.ParseFromJSON(doc, &parser)) << "Parse error: " << parser.error_str();

  auto& root_env = config.environment();

  // check the logger_options defaults with null is supplied to logger_options
  EXPECT_EQ(root_env.logger_options().filters().verbosity(), 0);
  EXPECT_TRUE(root_env.logger_options().filters().tags().empty());
}

TEST_F(ModelTest, ServiceBadUrl) {
  const char* json = R"({"environment":{"services":{"some.service":"blablabla"}}})";
  ExpectFailedParse(json, "service with bad url accepted");
}

TEST_F(ModelTest, LaunchAppGetOrDefault) {
  const char* json1 = R"({"url":"fuchsia-pkg://fuchsia.com/some_url#meta/some_url.cmx"})";
  json::JSONParser parser;
  auto doc1 = parser.ParseFromString(json1, "LaunchApGetOrDefault");
  config::LaunchApp app1;

  EXPECT_FALSE(parser.HasError()) << "Parse error: " << parser.error_str();
  EXPECT_TRUE(app1.ParseFromJSON(doc1, &parser)) << "Parse error: " << parser.error_str();

  const char* json2 = R"({"url":""})";
  auto doc2 = parser.ParseFromString(json2, "LaunchApGetOrDefault");

  config::LaunchApp app2;
  EXPECT_FALSE(parser.HasError()) << "Parse error: " << parser.error_str();
  EXPECT_TRUE(app2.ParseFromJSON(doc2, &parser)) << "Parse error: " << parser.error_str();

  const char* fallback = "fuchsia-pkg://fuchsia.com/fallback#meta/fallback.cmx";
  EXPECT_EQ(app1.GetUrlOrDefault(fallback), "fuchsia-pkg://fuchsia.com/some_url#meta/some_url.cmx");
  EXPECT_EQ(app2.GetUrlOrDefault(fallback), fallback);
}

TEST_F(ModelTest, TimeoutParsing) {
  const char* jsonbad = R"({"timeout": -10})";
  ExpectFailedParse(jsonbad, "negative timeout value accepted");

  const char* jsongood = R"({"timeout": 10})";
  json::JSONParser parser;
  auto doc = parser.ParseFromString(jsongood, "Good timeout JSON");
  config::Config config;
  EXPECT_FALSE(parser.HasError()) << "Parse error: " << parser.error_str();
  EXPECT_TRUE(config.ParseFromJSON(doc, &parser)) << "Parse error: " << parser.error_str();
  EXPECT_EQ(config.timeout(), zx::sec(10));
}

TEST_F(ModelTest, CaptureParsing) {
  Config config;
  ExpectSuccessfulParse(R"({"capture" : true})", &config);
  EXPECT_EQ(config.capture(), CaptureMode::ON_ERROR);
  ExpectSuccessfulParse(R"({"capture" : false})", &config);
  EXPECT_EQ(config.capture(), CaptureMode::NONE);
  ExpectSuccessfulParse(R"({"capture" : "NO"})", &config);
  EXPECT_EQ(config.capture(), CaptureMode::NONE);
  ExpectSuccessfulParse(R"({"capture" : "ON_ERROR"})", &config);
  EXPECT_EQ(config.capture(), CaptureMode::ON_ERROR);
  ExpectSuccessfulParse(R"({"capture" : "ALWAYS"})", &config);
  EXPECT_EQ(config.capture(), CaptureMode::ALWAYS);
  ExpectFailedParse(R"({"capture" : "foo"})", "Can't parse bad capture value");
}

TEST_F(ModelTest, InvalidKeys) {
  ExpectFailedParse(R"({ "foo" : "bar" })", "Bad config key accepted");
  ExpectFailedParse(R"({ "environment" : {"foo" : "bar"} })", "Bad environment key accepted");
  ExpectFailedParse(R"({ "networks" : [{"name" : "net", "foo" : "bar"}] })",
                    "Bad network key accepted");
  ExpectFailedParse(R"({ "environment" : { "setup": [{"foo" : "bar"}] } })",
                    "Bad launch_app key accepted");
  ExpectFailedParse(
      R"({ "networks" : [ {"name" : "net", "endpoints" : [{"name" : "ep", "foo" : "bar"}] }] })",
      "Bad endpoint key accepted");
}

TEST_F(ModelTest, InvalidGuestConfig) {
  ExpectFailedParse(R"({"guest" : {}})", "Guest model accepted non-array guest config");
  ExpectFailedParse(R"({"guest" : [{"files" : ["file1", "file2"]}]})",
                    "Guest model accepted non-object file definitions");
  ExpectFailedParse(R"({"guest" : [{"networks" : ["net1", "net2"]}]})",
                    "Guest model accepted too many ethertap networks");
  ExpectFailedParse(R"({"guest" : [{"networks" : [{}] }]})",
                    "Guest model accepted non-string network name");
  ExpectFailedParse(R"({"guest" : [{"bogus_key" : []}]})",
                    "Guest model accepted too many ethertap networks");
  ExpectFailedParse(R"({"guest" : [{"macs": []}]})",
                    "Guest model accepted non-object macs definition");
  ExpectFailedParse(R"({"guest" : [{"macs": {"00:11:22:33:44:55": {}}}]})",
                    "Guest model accepted non-string mac mapping");
}

}  // namespace testing
}  // namespace config
}  // namespace netemul
