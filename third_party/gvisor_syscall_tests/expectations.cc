#include "expectations.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <re2/re2.h>

#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/types/variant.h"

namespace netstack_syscall_test {

absl::StatusOr<TestSelector> TestSelector::Parse(absl::string_view name) {
  static const RE2& parse_expr = *[]() {
    const RE2* re = new RE2(R"regex((\w+|\*)/(\w+|\*)\.(\w+|\*)/(\w+|\*)|(\w+|\*)\.(\w+|\*))regex");
    assert(re->ok());
    return re;
  }();

  std::array<absl::string_view, 4> parameterized;
  std::array<absl::string_view, 2> unparameterized;

  // Wrapper around absl::string_view to interface with RE2. This is not
  // necessary in google3 but is in Fuchsia where OSS RE2 lacks some features.
  struct StringPiece {
    explicit StringPiece(absl::string_view& dest) : dest(dest) {}
    bool ParseFrom(const char* start, size_t len) {
      dest = absl::string_view(start, len);
      return true;
    }
    absl::string_view& dest;
  };

  {
    std::array<StringPiece, 6> parse_dest = {
        StringPiece(parameterized[0]),   StringPiece(parameterized[1]),
        StringPiece(parameterized[2]),   StringPiece(parameterized[3]),
        StringPiece(unparameterized[0]), StringPiece(unparameterized[1])};

    if (!RE2::FullMatch(name, parse_expr, &parse_dest[0], &parse_dest[1], &parse_dest[2],
                        &parse_dest[3], &parse_dest[4], &parse_dest[5])) {
      return absl::InvalidArgumentError(
          absl::StrFormat("Unrecognized name format, expecting either "
                          "\"A/B.C/D\" or \"A.B\": %s",
                          name));
    }
  }

  if (parameterized[0].empty()) {
    if (unparameterized[0] == "*") {
      return absl::InvalidArgumentError(
          absl::StrFormat("Wildcard cannot appear in first position in %s", name));
    }
    return TestSelector(std::string(unparameterized[1] == "*" ? unparameterized[0] : name));
  }

  std::optional<int> wildcard_at;
  for (int i = 0; i < parameterized.size(); ++i) {
    if (wildcard_at.has_value()) {
      if (parameterized[i] != "*") {
        return absl::InvalidArgumentError(
            absl::StrFormat("component %d is * but %d is not for %s", *wildcard_at, i, name));
      }
    } else {
      if (parameterized[i] == "*") {
        wildcard_at = i;
      }
    }
  }
  switch (wildcard_at.value_or(4)) {
    case 0:
      return absl::InvalidArgumentError(
          absl::StrFormat("Wildcard cannot appear in first position in %s", name));
    case 1:
      return TestSelector(Parameterized(std::string(parameterized[0])));
    case 2:
      return TestSelector(
          Parameterized(absl::StrFormat("%s/%s", parameterized[0], parameterized[1])));
    case 3:
      return TestSelector(Parameterized(
          absl::StrFormat("%s/%s.%s", parameterized[0], parameterized[1], parameterized[2])));
    case 4:
      return TestSelector(Parameterized(absl::StrFormat(
          "%s/%s.%s/%s", parameterized[0], parameterized[1], parameterized[2], parameterized[3])));
    default:
      return absl::InternalError(absl::StrFormat("Failed to parse: %s", name));
  }
}

std::vector<TestSelector> TestSelector::Selectors() && {
  std::vector<TestSelector> output;
  output.reserve(4);
  if (const auto* parameterized = std::get_if<Parameterized>(&prefix_); parameterized) {
    const std::string& prefix = parameterized->prefix;
    for (int i = 0; i < prefix.size(); ++i) {
      if (prefix[i] == '.' || prefix[i] == '/') {
        output.push_back(TestSelector(Parameterized(prefix.substr(0, i))));
      }
    }
  } else {
    const std::string& prefix = std::get<std::string>(prefix_);
    if (auto at = prefix.find('.'); at != std::string::npos) {
      output.emplace_back(TestSelector(prefix.substr(0, at)));
    }
  }
  output.emplace_back(*this);
  return output;
}

TestSelector TestSelector::ParameterizedTest(absl::string_view suite, absl::string_view name,
                                             absl::string_view test_case,
                                             absl::string_view parameter) {
  return TestSelector(
      Parameterized(absl::StrFormat("%s/%s.%s/%s", suite, name, test_case, parameter)));
}

TestSelector TestSelector::Test(absl::string_view name, absl::string_view test_case) {
  return TestSelector(absl::StrFormat("%s.%s", name, test_case));
}

TestSelector::TestSelector(std::variant<Parameterized, std::string> prefix)
    : prefix_(std::move(prefix)) {}

bool operator==(const TestSelector& lhs, const TestSelector& rhs) {
  return lhs.prefix_ == rhs.prefix_;
}

std::ostream& operator<<(std::ostream& out, const TestSelector& selector) {
  absl::string_view prefix_out;
  if (const auto* prefix = std::get_if<TestSelector::Parameterized>(&selector.prefix_); prefix) {
    prefix_out = prefix->prefix;
  } else {
    prefix_out = std::get<std::string>(selector.prefix_);
  }
  return out << "TestSelector(" << prefix_out << ')';
}

void AddExpectations(TestMap& map, TestSelector test_selector, TestOption expect) {
  map.insert(std::make_pair(std::move(test_selector), expect));
}

std::string TestName(const testing::TestInfo& info) {
  return absl::StrCat(info.test_suite_name(), ".", info.name());
}

std::vector<TestSelector> SelectorsForTest(const testing::TestInfo& info) {
  std::pair<absl::string_view, absl::string_view> test_suite_name =
      absl::StrSplit(info.test_suite_name(), '/');
  auto& [suite, name] = test_suite_name;

  if (const char* param = info.value_param(); param != nullptr) {
    std::pair<absl::string_view, absl::string_view> test_case_parameter =
        absl::StrSplit(info.name(), '/');
    auto& [test_case, parameter] = test_case_parameter;
    return TestSelector::ParameterizedTest(suite, name, test_case, parameter).Selectors();
  } else {
    return TestSelector::Test(suite, info.name()).Selectors();
  }
}

std::tuple<std::string, TestMap::const_iterator> GetTestNameAndExpectation(
    const testing::TestInfo& info, const TestMap& expectations) {
  for (const auto& selector : SelectorsForTest(info)) {
    auto it = expectations.find(selector);
    if (it != expectations.end()) {
      return std::make_tuple(TestName(info), it);
    }
  }
  return std::make_tuple(TestName(info), expectations.end());
}

// Only tests marked as `kSkip` in `tests` are not included in the filter,
// causing them to be skipped by GUnit.
// Tests are expected to pass if they are neither skipped nor expected to fail.
// These tests are not added to the `tests` map, allowing us to auto include
// newly added tests upstream.
std::optional<std::string> CreateNetstackTestFilters(const TestMap& expectations) {
  std::stringstream build_filters;

  absl::flat_hash_set<std::reference_wrapper<const TestMap::key_type>> used_expectations;

  const auto* instance = testing::UnitTest::GetInstance();
  for (int i = 0; i < instance->total_test_suite_count(); i++) {
    const auto* suite = instance->GetTestSuite(i);
    for (int j = 0; j < suite->total_test_count(); j++) {
      const auto* test = suite->GetTestInfo(j);

      auto [test_name, expectation_it] = GetTestNameAndExpectation(*test, expectations);

      if (expectation_it != expectations.end()) {
        used_expectations.insert(std::cref(expectation_it->first));
      }

      // Only tests explicitly marked as `kSkip` are skipped.
      if (expectation_it != expectations.end() && expectation_it->second == TestOption::kSkip) {
        continue;
      }
      if (build_filters.rdbuf()->in_avail() != 0) {
        build_filters << ":";
      }
      // Use full test name instead of fixture name to correctly filter test.
      build_filters << suite->name() << "." << test->name();
    }
  }

  bool has_unknown_expectations = false;
  for (const auto& [selector, expectation] : expectations) {
    if (used_expectations.find(selector) == used_expectations.end()) {
      has_unknown_expectations = true;
      std::cerr << "[ SYSCALL EXPECTATION FOR UNKNOWN TEST ] test expectation for " << selector
                << " was set to " << TestOptionToString(expectation) << ", but no test matched it"
                << std::endl;
    }
  }

  if (has_unknown_expectations) {
    std::cerr << "All test cases:\n";
    for (int i = 0; i < instance->total_test_suite_count(); i++) {
      const auto* suite = instance->GetTestSuite(i);
      for (int j = 0; j < suite->total_test_count(); j++) {
        const auto* test = suite->GetTestInfo(j);

        std::cerr << "  " << TestName(*test) << "\n";
      }
    }

    return {};
  }

  return build_filters.str();
}

}  // namespace netstack_syscall_test
