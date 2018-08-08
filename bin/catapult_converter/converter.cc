// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "converter.h"

#include <getopt.h>
#include <math.h>
#include <algorithm>
#include <map>
#include <numeric>
#include <vector>

#include "lib/fxl/logging.h"
#include "lib/fxl/random/uuid.h"
#include "lib/fxl/strings/string_printf.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/error/en.h"
#include "third_party/rapidjson/rapidjson/filereadstream.h"
#include "third_party/rapidjson/rapidjson/filewritestream.h"
#include "third_party/rapidjson/rapidjson/prettywriter.h"

namespace {

// Calculate the variance, with Bessel's correction applied.  Bessel's
// correction gives us a better estimation of the population's variance
// given a sample of the population.
double Variance(const std::vector<double>& values, double mean) {
  // For 0 or 1 sample values, the variance value (with Bessel's
  // correction) is not defined.  Rather than returning a NaN or Inf value,
  // which are not permitted in JSON, just return 0.
  if (values.size() <= 1)
    return 0;

  double sum_of_squared_diffs = 0.0;
  for (double value : values) {
    double diff = value - mean;
    sum_of_squared_diffs += diff * diff;
  }
  return sum_of_squared_diffs / static_cast<double>(values.size() - 1);
}

void WriteJson(FILE* fp, rapidjson::Document* doc) {
  char buffer[100];
  rapidjson::FileWriteStream output_stream(fp, buffer, sizeof(buffer));
  rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(output_stream);
  doc->Accept(writer);
}

// rapidjson's API is rather verbose to use.  This class provides some
// convenience wrappers.
class JsonHelper {
 public:
  explicit JsonHelper(rapidjson::Document::AllocatorType& alloc)
      : alloc_(alloc) {}

  rapidjson::Value MakeString(const char* string) {
    rapidjson::Value value;
    value.SetString(string, alloc_);
    return value;
  };

  rapidjson::Value Copy(const rapidjson::Value& value) {
    return rapidjson::Value(value, alloc_);
  }

 private:
  rapidjson::Document::AllocatorType& alloc_;
};

void ConvertSpacesToUnderscores(std::string* string) {
  for (size_t index = 0; index < string->size(); ++index) {
    if ((*string)[index] == ' ')
      (*string)[index] = '_';
  }
}

void ComputeStatistics(const std::vector<double>& vals,
                       rapidjson::Value* output,
                       rapidjson::Document::AllocatorType* alloc) {
  double sum = 0;
  double sum_of_logs = 0;

  for (auto val : vals) {
    sum += val;
    sum_of_logs += log(val);
  }

  double min = *std::min_element(vals.begin(), vals.end());
  double max = *std::max_element(vals.begin(), vals.end());
  double mean = sum / vals.size();
  // meanlogs is the mean of the logs of the values, which is useful for
  // calculating the geometric mean of the values.
  double meanlogs = sum_of_logs / vals.size();
  double variance = Variance(vals, mean);
  output->SetArray();
  output->PushBack(vals.size(), *alloc);  // "count" entry.
  output->PushBack(max, *alloc);
  output->PushBack(meanlogs, *alloc);
  output->PushBack(mean, *alloc);
  output->PushBack(min, *alloc);
  output->PushBack(sum, *alloc);
  output->PushBack(variance, *alloc);
}

// Takes the unit string as it appears in the input JSON file.  Returns the
// unit string that should be used in the Catapult Histogram JSON file.
// Converts the data as necessary.
std::string ConvertUnits(const char* input_unit, std::vector<double>* vals) {
  std::string catapult_unit;
  if (strcmp(input_unit, "nanoseconds") == 0 || strcmp(input_unit, "ns") == 0) {
    // Convert from nanoseconds to milliseconds.
    for (auto& val : *vals) {
      val /= 1e6;
    }
    return "ms_smallerIsBetter";
  } else if (strcmp(input_unit, "milliseconds") == 0 ||
             strcmp(input_unit, "ms") == 0) {
    return "ms_smallerIsBetter";
  } else if (strcmp(input_unit, "bytes/second") == 0) {
    // Convert from bytes/second to mebibytes/second.
    for (auto& val : *vals) {
      val /= 1024 * 1024;
    }

    // The Catapult dashboard does not yet support a "bytes per unit time"
    // unit (of any multiple), and it rejects unknown units, so we report
    // this as "unitless" here for now.  TODO(mseaborn): Add support for
    // data rate units to Catapult.
    return "unitless_biggerIsBetter";
  } else {
    fprintf(stderr, "Units not recognized: %s\n", input_unit);
    exit(1);
  }
}

// Adds a Histogram to the given |output| Document.
void AddHistogram(rapidjson::Document* output,
                  rapidjson::Document::AllocatorType* alloc,
                  const std::string& test_name, const char* input_unit,
                  std::vector<double>&& vals, rapidjson::Value diagnostic_map,
                  rapidjson::Value guid) {
  std::string catapult_unit = ConvertUnits(input_unit, &vals);
  rapidjson::Value stats;
  ComputeStatistics(vals, &stats, alloc);

  rapidjson::Value histogram;
  histogram.SetObject();
  histogram.AddMember("name", test_name, *alloc);
  histogram.AddMember("unit", catapult_unit, *alloc);
  histogram.AddMember("description", "", *alloc);
  histogram.AddMember("diagnostics", diagnostic_map, *alloc);
  histogram.AddMember("running", stats, *alloc);
  histogram.AddMember("guid", guid, *alloc);

  // This field is redundant with the "count" entry in "stats".
  histogram.AddMember("maxNumSampleValues", vals.size(), *alloc);

  // Assume for now that we didn't get any NaN values.
  histogram.AddMember("numNans", 0, *alloc);

  output->PushBack(histogram, *alloc);
}

}  // namespace

void Convert(rapidjson::Document* input, rapidjson::Document* output,
             const ConverterArgs* args) {
  rapidjson::Document::AllocatorType& alloc = output->GetAllocator();
  JsonHelper helper(alloc);
  output->SetArray();

  uint32_t next_dummy_guid = 0;
  auto MakeUuid = [&]() {
    std::string uuid;
    if (args->use_test_guids) {
      uuid = fxl::StringPrintf("dummy_guid_%d", next_dummy_guid++);
    } else {
      uuid = fxl::GenerateUUID();
    }
    return helper.MakeString(uuid.c_str());
  };

  // Add a "diagnostic" entry representing the given value.  Returns a GUID
  // value identifying the diagnostic.
  auto AddDiagnostic = [&](rapidjson::Value value) -> rapidjson::Value {
    rapidjson::Value guid = MakeUuid();

    // Add top-level description.
    rapidjson::Value diagnostic;
    diagnostic.SetObject();
    diagnostic.AddMember("guid", helper.Copy(guid), alloc);
    diagnostic.AddMember("type", "GenericSet", alloc);
    rapidjson::Value values;
    values.SetArray();
    values.PushBack(value, alloc);
    diagnostic.AddMember("values", values, alloc);
    output->PushBack(diagnostic, alloc);

    return guid;
  };

  // Build a JSON object containing the "diagnostic" values that are common
  // to all the test cases.
  rapidjson::Value shared_diagnostic_map;
  shared_diagnostic_map.SetObject();
  auto AddSharedDiagnostic = [&](const char* key, rapidjson::Value value) {
    auto guid = AddDiagnostic(std::move(value));
    shared_diagnostic_map.AddMember(helper.MakeString(key), guid, alloc);
  };
  rapidjson::Value timestamp;
  timestamp.SetInt64(args->timestamp);
  AddSharedDiagnostic("chromiumCommitPositions", std::move(timestamp));
  AddSharedDiagnostic("bots", helper.MakeString(args->bots));
  AddSharedDiagnostic("masters", helper.MakeString(args->masters));

  // The "logUrls" diagnostic contains a list of [name, url] tuples.
  rapidjson::Value log_url_array;
  log_url_array.SetArray();
  log_url_array.PushBack(helper.MakeString("Build Log"), alloc);
  log_url_array.PushBack(helper.MakeString(args->log_url), alloc);
  AddSharedDiagnostic("logUrls", std::move(log_url_array));

  // Allocate a GUID for the given test suite name (by creating a
  // "diagnostic" entry).  Memoize this allocation so that we don't
  // allocate >1 GUID for the same test suite name.
  std::map<std::string, rapidjson::Value> test_suite_to_guid;
  auto MakeGuidForTestSuiteName = [&](const char* test_suite) {
    auto it = test_suite_to_guid.find(test_suite);
    if (it != test_suite_to_guid.end()) {
      return helper.Copy(it->second);
    }
    rapidjson::Value guid = AddDiagnostic(helper.MakeString(test_suite));
    test_suite_to_guid[test_suite] = helper.Copy(guid);
    return guid;
  };

  for (auto& element : input->GetArray()) {
    // The new schema has a member "values" which is a list of floating point
    // numbers.
    if (element.HasMember("values")) {
      std::string name = element["label"].GetString();
      ConvertSpacesToUnderscores(&name);

      // The "test_suite" field in the input becomes the "benchmarks"
      // diagnostic in the output.
      rapidjson::Value test_suite_guid =
          MakeGuidForTestSuiteName(element["test_suite"].GetString());
      rapidjson::Value diagnostic_map = helper.Copy(shared_diagnostic_map);
      diagnostic_map.AddMember("benchmarks", test_suite_guid, alloc);

      const rapidjson::Value& values = element["values"].GetArray();
      if (values.Size() == 0) {
        fprintf(stderr, "Input 'values' is empty");
        exit(1);
      }

      std::vector<double> vals;
      vals.reserve(values.Size());
      for (auto& val : values.GetArray()) {
        vals.push_back(val.GetDouble());
      }

      // Create the histogram.
      if (element.HasMember("split_first") && element["split_first"].IsBool() &&
          element["split_first"].GetBool()) {
        // Create a histogram for the first sample value.
        std::string h1_name = name + "_samples_0_to_0";
        std::vector<double> h1_vals(vals.begin(), vals.begin() + 1);
        AddHistogram(output, &alloc, h1_name, element["unit"].GetString(),
                     std::move(h1_vals), helper.Copy(diagnostic_map),
                     MakeUuid());

        // Create a histogram for the remaining sample values, if any.
        if (vals.size() > 1) {
          std::stringstream h2_name;
          h2_name << name << "_samples_1_to_" << vals.size() - 1;

          std::vector<double> h2_vals(vals.begin() + 1, vals.end());
          AddHistogram(output, &alloc, h2_name.str(),
                       element["unit"].GetString(), std::move(h2_vals),
                       helper.Copy(diagnostic_map), MakeUuid());
        }
      } else {
        // Create a histogram for all |vals|.
        AddHistogram(output, &alloc, name, element["unit"].GetString(),
                     std::move(vals), std::move(diagnostic_map), MakeUuid());
      }
    } else {
      // Convert the old schema.
      // TODO(IN-452): Migrate existing tests to the new schema and delete this.

      uint32_t inner_label_count = 0;
      for (auto& sample : element["samples"].GetArray()) {
        std::string name = element["label"].GetString();
        // Generate a compound name if there is an inner label as well as an
        // outer label.
        if (sample.HasMember("label")) {
          if (sample["label"].GetStringLength() == 0) {
            fprintf(stderr, "Inner label field is empty\n");
            exit(1);
          }
          name += "_";
          name += sample["label"].GetString();
          ++inner_label_count;
        }
        ConvertSpacesToUnderscores(&name);

        // The "test_suite" field in the input becomes the "benchmarks"
        // diagnostic in the output.
        rapidjson::Value test_suite_guid =
            MakeGuidForTestSuiteName(element["test_suite"].GetString());
        rapidjson::Value diagnostic_map = helper.Copy(shared_diagnostic_map);
        diagnostic_map.AddMember("benchmarks", test_suite_guid, alloc);

        const rapidjson::Value& values = sample["values"];
        std::vector<double> vals;
        vals.reserve(values.Size());
        for (auto& val : values.GetArray()) {
          vals.push_back(val.GetDouble());
        }

        AddHistogram(output, &alloc, name, element["unit"].GetString(),
                     std::move(vals), std::move(diagnostic_map), MakeUuid());
      }

      size_t samples_size = element["samples"].GetArray().Size();
      if (samples_size > 1 && inner_label_count != samples_size) {
        fprintf(stderr, "Some entries in 'samples' array lack labels\n");
        exit(1);
      }
    }
  }
}

int ConverterMain(int argc, char** argv) {
  const char* usage =
      "Usage: %s [options]\n"
      "\n"
      "This tool takes results from Fuchsia performance tests (in Fuchsia's "
      "JSON perf test results format) and converts them to the Catapult "
      "Dashboard's JSON HistogramSet format.\n"
      "\n"
      "Options:\n"
      "  --input FILENAME\n"
      "      Input file: perf test results JSON file (required)\n"
      "  --output FILENAME\n"
      "      Output file: Catapult HistogramSet JSON file (default is stdout)\n"
      "\n"
      "The following are required and specify parameters to copy into the "
      "output file:\n"
      "  --execution-timestamp-ms NUMBER\n"
      "  --masters STRING\n"
      "  --bots STRING\n"
      "  --log-url URL\n"
      "See README.md for the meanings of these parameters.\n";

  // Parse command line arguments.
  static const struct option opts[] = {
      {"help", no_argument, nullptr, 'h'},
      {"input", required_argument, nullptr, 'i'},
      {"output", required_argument, nullptr, 'o'},
      {"execution-timestamp-ms", required_argument, nullptr, 'e'},
      {"masters", required_argument, nullptr, 'm'},
      {"bots", required_argument, nullptr, 'b'},
      {"log-url", required_argument, nullptr, 'l'},
  };
  ConverterArgs args;
  const char* input_filename = nullptr;
  const char* output_filename = nullptr;
  optind = 1;
  for (;;) {
    int opt = getopt_long(argc, argv, "h", opts, nullptr);
    if (opt < 0)
      break;
    switch (opt) {
      case 'h':
        printf(usage, argv[0]);
        return 0;
      case 'i':
        input_filename = optarg;
        break;
      case 'o':
        output_filename = optarg;
        break;
      case 'e':
        args.timestamp = strtoll(optarg, nullptr, 0);
        break;
      case 'm':
        args.masters = optarg;
        break;
      case 'b':
        args.bots = optarg;
        break;
      case 'l':
        args.log_url = optarg;
        break;
    }
  }
  if (optind < argc) {
    fprintf(stderr, "Unrecognized argument: \"%s\"\n", argv[optind]);
    return 1;
  }

  // Check arguments.
  bool failed = false;
  if (!input_filename) {
    fprintf(stderr, "--input argument is required\n");
    failed = true;
  }
  if (!args.timestamp) {
    fprintf(stderr, "--execution-timestamp-ms argument is required\n");
    failed = true;
  }
  if (!args.masters) {
    fprintf(stderr, "--masters argument is required\n");
    failed = true;
  }
  if (!args.bots) {
    fprintf(stderr, "--bots argument is required\n");
    failed = true;
  }
  if (!args.log_url) {
    fprintf(stderr, "--log-url argument is required\n");
    failed = true;
  }
  if (failed) {
    fprintf(stderr, "\n");
    fprintf(stderr, usage, argv[0]);
    return 1;
  }

  // Read input file.
  FILE* fp = fopen(input_filename, "r");
  if (!fp) {
    fprintf(stderr, "Failed to open input file, \"%s\"\n", input_filename);
    return 1;
  }
  char buffer[100];
  rapidjson::FileReadStream input_stream(fp, buffer, sizeof(buffer));
  rapidjson::Document input;
  rapidjson::ParseResult parse_result = input.ParseStream(input_stream);
  if (!parse_result) {
    fprintf(stderr, "Failed to parse input file, \"%s\": %s (offset %zd)\n",
            input_filename, rapidjson::GetParseError_En(parse_result.Code()),
            parse_result.Offset());
    return 1;
  }
  fclose(fp);

  rapidjson::Document output;
  Convert(&input, &output, &args);

  // Write output.
  if (output_filename) {
    fp = fopen(output_filename, "w");
    if (!fp) {
      fprintf(stderr, "Failed to open output file, \"%s\"\n", output_filename);
      return 1;
    }
    WriteJson(fp, &output);
    fclose(fp);
  } else {
    WriteJson(stdout, &output);
  }

  return 0;
}
