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
  static fpromise::result<std::unique_ptr<Lookup>, Lookup::Status> New(
      const std::vector<std::string>& locale_ids);
  fpromise::result<std::string_view, Lookup::Status> String(uint64_t message_id);
};

};  // namespace intl
```

## Sample program

A sample stand-alone program that demonstrates how lookup works. Sadly it
can not be run as a CFv2 component on the device, apparently. So it is for
illustrative purposes only.

### Prerequisites

* `--with=//src/lib/intl` in your `fx set` command line.
* `fx build`

