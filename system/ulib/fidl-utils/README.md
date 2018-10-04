libfidl-utils
================

fidl-utils contains C++ wrappers around libfidl.

fidl-utils may have dependencies that are not exported
in the SDK, and it may use C++17 features, where it would
not be able to do so if it was combined with libfidl.
