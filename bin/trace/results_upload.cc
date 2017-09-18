// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/results_upload.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "lib/network/fidl/url_loader.fidl.h"
#include "lib/network/fidl/url_request.fidl.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fsl/vmo/strings.h"

namespace tracing {

namespace {

const char kAddPointApi[] = "/add_point";

// https://github.com/catapult-project/catapult/blob/master/dashboard/docs/data-format.md
const char kMasterKey[] = "master";
const char kBotKey[] = "bot";
const char kTestSuiteNameKey[] = "test_suite_name";
const char kPointIdKey[] = "point_id";
const char kVersionsKey[] = "versions";
const char kSupplementalKey[] = "supplemental";
const char kChartDataKey[] = "chart_data";
const char kFormatVersionKey[] = "format_version";
const char kVersion1[] = "1.0";
const char kChartsKey[] = "charts";
const char kUnitsKey[] = "units";
const char kTypeKey[] = "type";
const char kScalar[] = "scalar";
const char kValueKey[] = "value";
const char kListOfScalarValues[] = "list_of_scalar_values";
const char kValuesKey[] = "values";

void EncodeSingle(rapidjson::Writer<rapidjson::StringBuffer>* writer,
                  const measure::Result& result) {
  writer->Key(result.label.c_str());
  writer->StartObject();
  {
    for (const measure::SampleGroup& sample_group : result.samples) {
      writer->Key(sample_group.label.c_str());
      writer->StartObject();
      {
        if (sample_group.values.size() == 1) {
          writer->Key(kTypeKey);
          writer->String(kScalar);

          writer->Key(kValueKey);
          writer->Double(sample_group.values.front());
        } else {
          writer->Key(kTypeKey);
          writer->String(kListOfScalarValues);

          writer->Key(kValuesKey);
          writer->StartArray();
          for (double value : sample_group.values) {
            writer->Double(value);
          }
          writer->EndArray();
        }

        writer->Key(kUnitsKey);
        writer->String(result.unit);
      }
      writer->EndObject();
    }
  }
  writer->EndObject();
}

std::string Encode(const UploadMetadata& upload_metadata,
                   const std::vector<measure::Result>& results) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();
  {
    writer.Key(kMasterKey);
    writer.String(upload_metadata.master.c_str());

    writer.Key(kBotKey);
    writer.String(upload_metadata.bot.c_str());

    writer.Key(kTestSuiteNameKey);
    writer.String(upload_metadata.test_suite_name.c_str());

    writer.Key(kPointIdKey);
    writer.Int(upload_metadata.point_id);

    // The dashboard endpoint requires this dictionary to be present even if
    // empty.
    writer.Key(kVersionsKey);
    writer.StartObject();
    writer.EndObject();

    // The dashboard endpoint requires this dictionary to be present even if
    // empty.
    writer.Key(kSupplementalKey);
    writer.StartObject();
    writer.EndObject();

    writer.Key(kChartDataKey);
    writer.StartObject();
    {
      writer.Key(kFormatVersionKey);
      writer.String(kVersion1);

      writer.Key(kChartsKey);
      writer.StartObject();
      {
        // Each result is an individual chart.
        for (const measure::Result& result : results) {
          if (result.samples.empty()) {
            continue;
          }

          EncodeSingle(&writer, result);
        }
      }
      writer.EndObject();
    }
    writer.EndObject();
  }
  writer.EndObject();

  return string_buffer.GetString();
}

network::URLRequestPtr MakeRequest(const std::string& server_url,
                                   const std::string& data) {
  network::URLRequestPtr request(network::URLRequest::New());
  request->url = server_url + kAddPointApi;
  request->method = "POST";

  std::string payload = "data=" + data;
  zx::vmo buffer;
  bool res = fsl::VmoFromString(payload, &buffer);
  if (!res) {
    return nullptr;
  }

  request->body = network::URLBody::New();
  request->body->set_buffer(std::move(buffer));
  return request;
}

}  // namespace

void UploadResults(std::ostream& out,
                   std::ostream& err,
                   network::NetworkServicePtr network_service,
                   const UploadMetadata& upload_metadata,
                   const std::vector<measure::Result>& results,
                   std::function<void(bool)> on_done) {
  network::URLLoaderPtr url_loader;
  network_service->CreateURLLoader(url_loader.NewRequest());
  network::URLLoader* url_loader_ptr = url_loader.get();
  network::URLRequestPtr url_request =
      MakeRequest(upload_metadata.server_url, Encode(upload_metadata, results));
  url_loader.set_connection_error_handler([on_done, &err] {
    err << "connection to url loader closed unexpectedly" << std::endl;
    on_done(false);
  });

  out << "starting upload to " << url_request->url << std::endl;
  url_loader_ptr->Start(
      std::move(url_request), fxl::MakeCopyable([
        url_loader = std::move(url_loader), on_done, &out, &err
      ](auto url_response) {
        out << "response";
        if (url_response->error) {
          err << url_response->url << " network error "
              << url_response->error->description << std::endl;
          on_done(false);
          return;
        }

        if (url_response->status_code != 200) {
          err << url_response->url << " url_response status "
              << url_response->status_code << std::endl;
          on_done(false);
          return;
        }

        out << "upload succeeded" << std::endl;
        on_done(true);
      }));
}

}  // namespace tracing
