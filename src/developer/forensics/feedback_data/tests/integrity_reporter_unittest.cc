// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/integrity_reporter.h"

#include <lib/fit/result.h>
#include <lib/syslog/cpp/macros.h>

#include <cstring>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"
#include "src/developer/forensics/utils/errors.h"
// TODO(fxbug.dev/57392): Move it back to //third_party once unification completes.
#include "zircon/third_party/rapidjson/include/rapidjson/document.h"
#include "zircon/third_party/rapidjson/include/rapidjson/schema.h"

#define ANNOTATIONS_JSON_STATE_IS(json, state)              \
  ASSERT_TRUE(json.HasMember("annotations.json"));          \
  ASSERT_TRUE(json["annotations.json"].HasMember("state")); \
  EXPECT_STREQ(json["annotations.json"]["state"].GetString(), state);

#define HAS_PRESENT_ANNOTATION(json, name)                                                      \
  ASSERT_TRUE(json.HasMember("annotations.json"));                                              \
  ASSERT_TRUE(json["annotations.json"].HasMember("present annotations"));                       \
  {                                                                                             \
    bool has_annotation = false;                                                                \
    for (const auto& annotation : json["annotations.json"]["present annotations"].GetArray()) { \
      if (strcmp(annotation.GetString(), name) == 0) {                                          \
        has_annotation = true;                                                                  \
        break;                                                                                  \
      }                                                                                         \
    }                                                                                           \
    EXPECT_TRUE(has_annotation&& name);                                                         \
  }

#define HAS_MISSING_ANNOTATION(json, name, reason)                              \
  ASSERT_TRUE(json.HasMember("annotations.json"));                              \
  ASSERT_TRUE(json["annotations.json"].HasMember("missing annotations"));       \
  ASSERT_TRUE(json["annotations.json"]["missing annotations"].HasMember(name)); \
  EXPECT_STREQ(json["annotations.json"]["missing annotations"][name].GetString(), reason);

#define HAS_COMPLETE_ATTACHMENT(json, name)   \
  ASSERT_TRUE(json.HasMember(name));          \
  ASSERT_TRUE(json[name].HasMember("state")); \
  EXPECT_STREQ(json[name]["state"].GetString(), "complete");

#define HAS_PARTIAL_ATTACHMENT(json, name, reason)          \
  ASSERT_TRUE(json.HasMember(name));                        \
  ASSERT_TRUE(json[name].HasMember("state"));               \
  EXPECT_STREQ(json[name]["state"].GetString(), "partial"); \
  ASSERT_TRUE(json[name].HasMember("reason"));              \
  EXPECT_STREQ(json[name]["reason"].GetString(), reason);

#define HAS_MISSING_ATTACHMENT(json, name, reason)          \
  ASSERT_TRUE(json.HasMember(name));                        \
  ASSERT_TRUE(json[name].HasMember("state"));               \
  EXPECT_STREQ(json[name]["state"].GetString(), "missing"); \
  ASSERT_TRUE(json[name].HasMember("reason"));              \
  EXPECT_STREQ(json[name]["reason"].GetString(), reason);

namespace forensics {
namespace feedback_data {
namespace {

constexpr char kIntegrityReportSchema[] = R"({
   "type":"object",
   "patternProperties":{
      "^.*$":{
         "type":"object",
         "properties":{
            "state":{
               "type":"string",
               "enum":[
                  "complete",
                  "partial",
                  "missing"
               ]
            },
            "reason":{
               "type":"string"
            }
         },
         "required":[
            "state"
         ]
      }
   },
   "properties":{
      "annotations.json":{
         "type":"object",
         "properties":{
            "state":{
               "type":"string",
               "enum":[
                  "complete",
                  "partial",
                  "missing"
               ]
            },
            "missing annotations":{
               "type":"object",
               "patternProperties":{
                  "^.*$":{
                     "type":"string"
                  }
               }
            },
            "present annotations":{
               "type":"array",
               "items":{
                  "type":"string"
               }
            }
         },
         "required":[
            "state",
            "missing annotations",
            "present annotations"
         ]
      }
   }
})";

// Get the integrity report for the provided annotations and attachments, check that it adheres to
// the schema, and turn it into a json document
rapidjson::Document MakeJsonReport(const IntegrityReporter& reporter,
                                   const ::fit::result<Annotations>& annotations,
                                   const ::fit::result<Attachments>& attachments,
                                   const bool missing_non_platform_annotations = false) {
  const auto integrity_report =
      reporter.MakeIntegrityReport(annotations, attachments, missing_non_platform_annotations);
  FX_CHECK(integrity_report.has_value());

  rapidjson::Document json;
  FX_CHECK(!json.Parse(integrity_report.value().c_str()).HasParseError());
  rapidjson::Document schema_json;
  FX_CHECK(!schema_json.Parse(kIntegrityReportSchema).HasParseError());
  rapidjson::SchemaDocument schema(schema_json);
  rapidjson::SchemaValidator validator(schema);
  FX_CHECK(json.Accept(validator));

  return json;
}

TEST(IntegrityReporterTest, Check_AddsMissingAnnotationsOnNoAnnotations) {
  const AnnotationKeys annotation_allowlist = {
      "annotation 1",
  };

  const IntegrityReporter reporter(annotation_allowlist, /*attachment_allowlist=*/{});

  const auto report = MakeJsonReport(reporter, ::fit::error(), ::fit::error());
  HAS_MISSING_ANNOTATION(report, "annotation 1", "feedback logic error");
}

TEST(IntegrityReporterTest, Check_AddsMissingAnnotationsOnEmptyAnnotations) {
  const AnnotationKeys annotation_allowlist = {
      "annotation 1",
  };

  const IntegrityReporter reporter(annotation_allowlist, /*attachment_allowlist=*/{});

  const auto report = MakeJsonReport(reporter, ::fit::ok<Annotations>({}), ::fit::error());
  HAS_MISSING_ANNOTATION(report, "annotation 1", "feedback logic error");
}

TEST(IntegrityReporterTest, Check_AddsMissingAttachmentsOnNoAttachments) {
  const AttachmentKeys attachment_allowlist = {
      "attachment 1",
  };

  IntegrityReporter reporter(/*annotation_allowlist=*/{}, attachment_allowlist);

  const auto report = MakeJsonReport(reporter, ::fit::error(), ::fit::error());
  HAS_MISSING_ATTACHMENT(report, "attachment 1", "feedback logic error");
}

TEST(IntegrityReporterTest, Check_AddsMissingAttachmentsOnEmptyAttachments) {
  const AttachmentKeys attachment_allowlist = {
      "attachment 1",
  };

  IntegrityReporter reporter(/*annotation_allowlist=*/{}, attachment_allowlist);

  const auto report = MakeJsonReport(reporter, ::fit::error(), ::fit::ok<Attachments>({}));
  HAS_MISSING_ATTACHMENT(report, "attachment 1", "feedback logic error");
}

TEST(IntegrityReporterTest, Check_FormatAnnotationsProperly) {
  const AnnotationKeys annotation_allowlist = {
      "present annotation 1",
      "present annotation 2",
      "missing annotation 1",
      "missing annotation 2",
  };

  const Annotations annotations = {
      {"present annotation 1", AnnotationOr("")},
      {"present annotation 2", AnnotationOr("")},
      {"missing annotation 1", AnnotationOr(Error::kConnectionError)},
      {"missing annotation 2", AnnotationOr(Error::kFileWriteFailure)},
  };

  IntegrityReporter reporter(annotation_allowlist, /*attachment_allowlist=*/{});

  const auto report = MakeJsonReport(reporter, ::fit::ok(std::move(annotations)), ::fit::error());

  ANNOTATIONS_JSON_STATE_IS(report, "partial");

  HAS_PRESENT_ANNOTATION(report, "present annotation 1");
  HAS_PRESENT_ANNOTATION(report, "present annotation 2");

  HAS_MISSING_ANNOTATION(report, "missing annotation 1", "FIDL connection error");
  HAS_MISSING_ANNOTATION(report, "missing annotation 2", "file write failure");
}

TEST(IntegrityReporterTest, Check_FormatAttachmentsProperly) {
  const AttachmentKeys attachment_allowlist = {
      "complete attachment 1", "complete attachment 2", "partial attachment 1",
      "partial attachment 2",  "missing attachment 1",  "missing attachment 2",
  };

  const Attachments attachments = {
      {"complete attachment 1", AttachmentValue("")},
      {"complete attachment 2", AttachmentValue("")},
      {"partial attachment 1", AttachmentValue("", Error::kTimeout)},
      {"partial attachment 2", AttachmentValue("", Error::kAsyncTaskPostFailure)},
      {"missing attachment 1", AttachmentValue(Error::kBadValue)},
      {"missing attachment 2", AttachmentValue(Error::kFileReadFailure)},
  };

  IntegrityReporter reporter(/*annotation_allowlist=*/{}, attachment_allowlist);

  const auto report =
      MakeJsonReport(reporter, ::fit::error(), ::fit::ok<Attachments>(std::move(attachments)));

  HAS_COMPLETE_ATTACHMENT(report, "complete attachment 1");
  HAS_COMPLETE_ATTACHMENT(report, "complete attachment 2");

  HAS_PARTIAL_ATTACHMENT(report, "partial attachment 1", "data collection timeout");
  HAS_PARTIAL_ATTACHMENT(report, "partial attachment 2", "async post task failure");

  HAS_MISSING_ATTACHMENT(report, "missing attachment 1", "bad data returned");
  HAS_MISSING_ATTACHMENT(report, "missing attachment 2", "file read failure");
}

TEST(IntegrityReporterTest, Check_NonPlatformAnnotationsComplete) {
  const Annotations annotations = {
      {"non-platform annotation", AnnotationOr("")},
  };

  IntegrityReporter reporter(/*annotation_allowlist=*/{}, /*attachment_allowlist=*/{});

  const auto report = MakeJsonReport(reporter, ::fit::ok(std::move(annotations)), ::fit::error());

  HAS_PRESENT_ANNOTATION(report, "non-platform annotations");
}

TEST(IntegrityReporterTest, Check_NonPlatformAnnotationsPartial) {
  const Annotations annotations = {
      {"non-platform annotation", AnnotationOr("")},
  };

  IntegrityReporter reporter(/*annotation_allowlist=*/{}, /*attachment_allowlist=*/{});

  const auto report = MakeJsonReport(reporter, ::fit::ok(std::move(annotations)), ::fit::error(),
                                     /*missing_non_platform_annotations=*/true);

  HAS_MISSING_ANNOTATION(report, "non-platform annotations",
                         "too many non-platfrom annotations added");
}

TEST(IntegrityReporterTest, Check_NonPlatformAnnotationsMissing) {
  IntegrityReporter reporter(/*annotation_allowlist=*/{}, /*attachment_allowlist=*/{});

  const auto report = MakeJsonReport(reporter, ::fit::error(), ::fit::error(),
                                     /*missing_non_platform_annotations=*/true);

  HAS_MISSING_ANNOTATION(report, "non-platform annotations",
                         "too many non-platfrom annotations added");
}

TEST(IntegrityReporterTest, Check_SmokeTest) {
  const AnnotationKeys annotation_allowlist = {
      "present annotation 1", "present annotation 2", "missing annotation 1",
      "missing annotation 2", "missing annotation 3",
  };

  const Annotations annotations = {
      {"present annotation 1", AnnotationOr("")},
      {"present annotation 2", AnnotationOr("")},
      {"missing annotation 1", AnnotationOr(Error::kConnectionError)},
      {"missing annotation 2", AnnotationOr(Error::kFileWriteFailure)},
      {"non-platform annotation 1", AnnotationOr("")},
  };

  const AttachmentKeys attachment_allowlist = {
      "complete attachment 1", "complete attachment 2", "partial attachment 1",
      "partial attachment 2",  "missing attachment 1",  "missing attachment 2",
      "missing attachment 3",
  };
  const Attachments attachments = {
      {"complete attachment 1", AttachmentValue("")},
      {"complete attachment 2", AttachmentValue("")},
      {"partial attachment 1", AttachmentValue("", Error::kTimeout)},
      {"partial attachment 2", AttachmentValue("", Error::kAsyncTaskPostFailure)},
      {"missing attachment 1", AttachmentValue(Error::kBadValue)},
      {"missing attachment 2", AttachmentValue(Error::kFileReadFailure)},
  };

  IntegrityReporter reporter(annotation_allowlist, attachment_allowlist);

  const auto report = MakeJsonReport(reporter, ::fit::ok<Annotations>(std::move(annotations)),
                                     ::fit::ok<Attachments>(std::move(attachments)),
                                     /*missing_non_platform_annotations=*/true);

  HAS_COMPLETE_ATTACHMENT(report, "complete attachment 1");
  HAS_COMPLETE_ATTACHMENT(report, "complete attachment 2");

  HAS_PARTIAL_ATTACHMENT(report, "partial attachment 1", "data collection timeout");
  HAS_PARTIAL_ATTACHMENT(report, "partial attachment 2", "async post task failure");

  HAS_MISSING_ATTACHMENT(report, "missing attachment 1", "bad data returned");
  HAS_MISSING_ATTACHMENT(report, "missing attachment 2", "file read failure");
  HAS_MISSING_ATTACHMENT(report, "missing attachment 3", "feedback logic error");

  ANNOTATIONS_JSON_STATE_IS(report, "partial");

  HAS_PRESENT_ANNOTATION(report, "present annotation 1");
  HAS_PRESENT_ANNOTATION(report, "present annotation 2");

  HAS_MISSING_ANNOTATION(report, "missing annotation 1", "FIDL connection error");
  HAS_MISSING_ANNOTATION(report, "missing annotation 2", "file write failure");
  HAS_MISSING_ANNOTATION(report, "missing annotation 3", "feedback logic error");

  HAS_MISSING_ANNOTATION(report, "non-platform annotations",
                         "too many non-platfrom annotations added");
}

TEST(IntegrityReporterTest, Fail_EmptyBugreport) {
  IntegrityReporter reporter(/*annotation_allowlist=*/{}, /*attachment_allowlist=*/{});

  const auto integrity_report = reporter.MakeIntegrityReport(
      ::fit::error(), ::fit::error(), /*missing_non_platform_annotations=*/false);
  EXPECT_FALSE(integrity_report.has_value());
}

struct TestParam {
  std::string test_name;
  AnnotationKeys annotation_allowlist;
  Annotations annotations;
  bool missing_non_platform_annotations;
  std::string state;
};

class AnnotationsJsonStateTest : public testing::Test,
                                 public testing::WithParamInterface<TestParam> {};

INSTANTIATE_TEST_SUITE_P(WithVariousAnnotations, AnnotationsJsonStateTest,
                         ::testing::ValuesIn(
                             std::vector<TestParam>(
                                 {
                                     TestParam{
                                         .test_name = "CompletePlatform_CompleteNonPlatform",
                                         .annotation_allowlist = {"platform"},
                                         .annotations =
                                             {
                                                 {"platform", AnnotationOr("")},
                                                 {"non-platform", AnnotationOr("")},
                                             },
                                         .missing_non_platform_annotations = false,
                                         .state = "complete",

                                     },
                                     TestParam{
                                         .test_name = "CompletePlatform_PartialNonPlatform",
                                         .annotation_allowlist = {"platform"},
                                         .annotations =
                                             {
                                                 {"platform", AnnotationOr("")},
                                                 {"non-platform", AnnotationOr("")},
                                             },
                                         .missing_non_platform_annotations = true,
                                         .state = "partial",

                                     },
                                     TestParam{
                                         .test_name = "CompletePlatform_MissingNonPlatform",
                                         .annotation_allowlist = {"platform"},
                                         .annotations =
                                             {
                                                 {"platform", AnnotationOr("")},
                                             },
                                         .missing_non_platform_annotations = true,
                                         .state = "partial",

                                     },
                                     TestParam{
                                         .test_name = "PartialPlatform_CompleteNonPlatform",
                                         .annotation_allowlist = {"platform 1", "platform 2"},
                                         .annotations =
                                             {
                                                 {"platform 1", AnnotationOr("")},
                                                 {"non-platform", AnnotationOr("")},
                                             },
                                         .missing_non_platform_annotations = false,
                                         .state = "partial",

                                     },
                                     TestParam{
                                         .test_name = "PartialPlatform_PartialNonPlatform",
                                         .annotation_allowlist = {"platform 1", "platform 2"},
                                         .annotations =
                                             {
                                                 {"platform 1", AnnotationOr("")},
                                                 {"non-platform", AnnotationOr("")},
                                             },
                                         .missing_non_platform_annotations = true,
                                         .state = "partial",

                                     },
                                     TestParam{
                                         .test_name = "PartialPlatform_MissingNonPlatform",
                                         .annotation_allowlist = {"platform 1", "platform 2"},
                                         .annotations =
                                             {
                                                 {"platform 1", AnnotationOr("")},
                                             },
                                         .missing_non_platform_annotations = true,
                                         .state = "partial",

                                     },
                                     TestParam{
                                         .test_name = "MissingPlatform_CompleteNonPlatform",
                                         .annotation_allowlist = {"platform"},
                                         .annotations =
                                             {
                                                 {"non-platform", AnnotationOr("")},
                                             },
                                         .missing_non_platform_annotations = false,
                                         .state = "partial",

                                     },
                                     TestParam{
                                         .test_name = "MissingPlatform_PartialNonPlatform",
                                         .annotation_allowlist = {"platform"},
                                         .annotations =
                                             {
                                                 {"non-platform", AnnotationOr("")},
                                             },
                                         .missing_non_platform_annotations = true,
                                         .state = "partial",

                                     },
                                     TestParam{
                                         .test_name = "MissingPlatform_MissingNonPlatform",
                                         .annotation_allowlist = {"platform"},
                                         .annotations = {},
                                         .missing_non_platform_annotations = true,
                                         .state = "missing",

                                     },
                                 })),
                         [](const testing::TestParamInfo<TestParam>& info) {
                           return info.param.test_name;
                         });
TEST_P(AnnotationsJsonStateTest, Succeed) {
  const auto param = GetParam();
  const IntegrityReporter reporter(param.annotation_allowlist, /*attachment_allowlist=*/{});

  const auto report = MakeJsonReport(reporter, ::fit::ok(param.annotations), ::fit::error(),
                                     param.missing_non_platform_annotations);
  ANNOTATIONS_JSON_STATE_IS(report, param.state.c_str());
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
