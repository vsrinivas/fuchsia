// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "converter.h"

#include <getopt.h>
#include <lib/syslog/cpp/macros.h>
#include <math.h>

#include <algorithm>
#include <map>
#include <numeric>
#include <vector>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"
#include "src/lib/fxl/strings/string_printf.h"

#if defined(OS_FUCHSIA)
#include <zircon/syscalls.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <src/lib/files/file_descriptor.h>
#include <src/lib/files/unique_fd.h>
#endif

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
  // Check that all the output was serialized successfully as JSON.  This
  // can fail if the output contained NaN or infinite floating point
  // values.
  FX_CHECK(writer.IsComplete());
}

// rapidjson's API is rather verbose to use.  This class provides some
// convenience wrappers.
class JsonHelper {
 public:
  explicit JsonHelper(rapidjson::Document::AllocatorType& alloc) : alloc_(alloc) {}

  rapidjson::Value MakeString(const char* string) {
    rapidjson::Value value;
    value.SetString(string, alloc_);
    return value;
  };

  rapidjson::Value Copy(const rapidjson::Value& value) { return rapidjson::Value(value, alloc_); }

 private:
  rapidjson::Document::AllocatorType& alloc_;
};

void ConvertSpacesToUnderscores(std::string* string) {
  for (size_t index = 0; index < string->size(); ++index) {
    if ((*string)[index] == ' ')
      (*string)[index] = '_';
  }
}

void ComputeStatistics(const std::vector<double>& vals, rapidjson::Value* output,
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
  double variance = Variance(vals, mean);

  // meanlogs is the mean of the logs of the values, which is useful for
  // calculating the geometric mean of the values.
  //
  // If any of the values are zero or negative, meanlogs will be -Infinity
  // or a NaN, which can't be serialized in JSON format.  In those cases,
  // we write 'null' in the JSON instead.
  double meanlogs = sum_of_logs / vals.size();
  rapidjson::Value meanlogs_json;
  if (isfinite(meanlogs))
    meanlogs_json.SetDouble(meanlogs);

  output->SetArray();
  output->PushBack(static_cast<uint64_t>(vals.size()),
                   *alloc);  // "count" entry.
  output->PushBack(max, *alloc);
  output->PushBack(meanlogs_json, *alloc);
  output->PushBack(mean, *alloc);
  output->PushBack(min, *alloc);
  output->PushBack(sum, *alloc);
  output->PushBack(variance, *alloc);
}

// Takes the unit string as it appears in the input JSON file.  Returns the
// unit string that should be used in the Catapult Histogram JSON file.
// Converts the data as necessary.
//
// The list of valid unit strings for the Catapult Histogram JSON format is
// available at:
// https://github.com/catapult-project/catapult/blob/8dc09eb0703647db9ca37b26f2d01a0a4dc0285c/tracing/tracing/value/histogram.py#L478
std::string ConvertUnits(const char* input_unit, std::vector<double>* vals) {
  std::string catapult_unit;
  if (strcmp(input_unit, "nanoseconds") == 0 || strcmp(input_unit, "ns") == 0) {
    // Convert from nanoseconds to milliseconds.
    for (auto& val : *vals) {
      val /= 1e6;
    }
    return "ms_smallerIsBetter";
  } else if (strcmp(input_unit, "milliseconds") == 0 || strcmp(input_unit, "ms") == 0) {
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
  } else if (strcmp(input_unit, "bytes") == 0) {
    return "sizeInBytes_smallerIsBetter";
  } else if (strcmp(input_unit, "frames/second") == 0) {
    return "Hz_biggerIsBetter";
  } else if (strcmp(input_unit, "percent") == 0) {
    return "n%_smallerIsBetter";
  } else if (strcmp(input_unit, "count") == 0) {
    return "count";
  } else if (strcmp(input_unit, "Watts") == 0) {
    return "W_smallerIsBetter";
  } else {
    fprintf(stderr, "Units not recognized: %s\n", input_unit);
    exit(1);
  }
}

// Adds a Histogram to the given |output| Document.
void AddHistogram(rapidjson::Document* output, rapidjson::Document::AllocatorType* alloc,
                  const std::string& test_name, const char* input_unit, std::vector<double>&& vals,
                  rapidjson::Value diagnostic_map, rapidjson::Value guid) {
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
  histogram.AddMember("maxNumSampleValues", static_cast<uint64_t>(vals.size()), *alloc);

  // Assume for now that we didn't get any NaN values.
  histogram.AddMember("numNans", 0, *alloc);

  output->PushBack(histogram, *alloc);
}

// Convert |type| into a string representation.
const char* TypeToString(rapidjson::Type type) {
  switch (type) {
    case rapidjson::kNullType:
      return "null";
    case rapidjson::kFalseType:
      return "false";
    case rapidjson::kTrueType:
      return "true";
    case rapidjson::kObjectType:
      return "object";
    case rapidjson::kArrayType:
      return "array";
    case rapidjson::kStringType:
      return "string";
    case rapidjson::kNumberType:
      return "number";
  }
  FX_NOTREACHED() << "Unexpected rapidjson type " << static_cast<int>(type);
  return "";
}

// Fills |output_length| bytes of |output| with random data.
void RandBytes(void* output, size_t output_length) {
  FX_DCHECK(output);

#if defined(OS_FUCHSIA)
  zx_cprng_draw(output, output_length);
#else
  fbl::unique_fd fd(open("/dev/urandom", O_RDONLY | O_CLOEXEC));
  FX_CHECK(fd.is_valid());
  const ssize_t len = fxl::ReadFileDescriptor(fd.get(), static_cast<char*>(output), output_length);
  FX_CHECK(len >= 0 && static_cast<size_t>(len) == output_length);
#endif
}

}  // namespace

// Code copied from "//src/lib/uuid/uuid.cc", however it uses our |RandBytes|
// (which uses "/dev/urandom" when running on non-Fuchsia platforms), as
// opposed to an unconditional |zx_cprng_draw|, so that we can support
// Linux/Mac platforms as well.
std::string GenerateUuid() {
  uint64_t bytes[2];
  RandBytes(bytes, sizeof(bytes));

  // Set the UUID to version 4 as described in RFC 4122, section 4.4.
  // The format of UUID version 4 must be xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx,
  // where y is one of [8, 9, A, B].
  // Clear the version bits and set the version to 4:
  bytes[0] &= 0xffffffffffff0fffULL;
  bytes[0] |= 0x0000000000004000ULL;

  // Set the two most significant bits (bits 6 and 7) of the
  // clock_seq_hi_and_reserved to zero and one, respectively:
  bytes[1] &= 0x3fffffffffffffffULL;
  bytes[1] |= 0x8000000000000000ULL;

  return fxl::StringPrintf("%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned int>(bytes[0] >> 32),
                           static_cast<unsigned int>((bytes[0] >> 16) & 0x0000ffff),
                           static_cast<unsigned int>(bytes[0] & 0x0000ffff),
                           static_cast<unsigned int>(bytes[1] >> 48),
                           bytes[1] & 0x0000ffffffffffffULL);
}

void Convert(rapidjson::Document* input, rapidjson::Document* output, const ConverterArgs* args) {
  rapidjson::Document::AllocatorType& alloc = output->GetAllocator();
  JsonHelper helper(alloc);
  output->SetArray();

  uint32_t next_dummy_guid = 0;
  auto MakeUuid = [&]() {
    std::string uuid;
    if (args->use_test_guids) {
      uuid = fxl::StringPrintf("dummy_guid_%d", next_dummy_guid++);
    } else {
      uuid = GenerateUuid();
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
  AddSharedDiagnostic("pointId", std::move(timestamp));
  AddSharedDiagnostic("bots", helper.MakeString(args->bots));
  AddSharedDiagnostic("masters", helper.MakeString(args->masters));
  if (args->product_versions) {
    AddSharedDiagnostic("a_productVersions", helper.MakeString(args->product_versions));
  }

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

  if (!input->IsArray()) {
    fprintf(stderr, "Expected input document to be of type array, and got %s instead\n",
            TypeToString(input->GetType()));
    exit(1);
  }

  for (auto& element : input->GetArray()) {
    std::string name = element["label"].GetString();
    ConvertSpacesToUnderscores(&name);

    // The "test_suite" field in the input becomes the "benchmarks"
    // diagnostic in the output.
    rapidjson::Value test_suite_guid = MakeGuidForTestSuiteName(element["test_suite"].GetString());
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
    // Create a histogram for all |vals|.
    AddHistogram(output, &alloc, name, element["unit"].GetString(), std::move(vals),
                 std::move(diagnostic_map), MakeUuid());
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
      "  --product-versions STRING\n"
      "      Release version in the format 0.yyyymmdd.a.b if applicable. e.g. 0.20200101.1.2\n"
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
      {"product-versions", required_argument, nullptr, 'v'},
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
      case 'v':
        args.product_versions = optarg;
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
    fprintf(stderr, "Failed to parse input file, \"%s\": %s (offset %zd)\n", input_filename,
            rapidjson::GetParseError_En(parse_result.Code()), parse_result.Offset());
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
