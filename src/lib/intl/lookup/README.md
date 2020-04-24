# Libraries for the "intl lookup" functionality

An interface for looking up localized message versions.

Proposed API:

```
namespace intl {

class Lookup {
 public:
  ~Lookup();
  enum class Status : int8_t {
    OK = 0,
    UNAVAILABLE = 1,
    ARGUMENT_ERROR = 2,
  };
  static fit::result<std::unique_ptr<Lookup>, Lookup::Status> New(
      const std::vector<std::string>& locale_ids);
  fit::result<std::string_view, Lookup::Status> String(uint64_t message_id);
};

};  // namespace intl
```
