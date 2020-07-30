#ifndef CTS_TESTS_HELLO_WORLD_HELLO_WORLD_UTIL_H_
#define CTS_TESTS_HELLO_WORLD_HELLO_WORLD_UTIL_H_
#include <string>

namespace {
class HelloWorldUtil {
 public:
  static std::string get_hello_world() { return "Hello, World!"; }
};
}  // namespace
#endif  // CTS_TESTS_HELLO_WORLD_HELLO_WORLD_UTIL_H_
