This folder contains the test data used to unit test the codesize tool.
So far, we have checked in a single `libasync-default.so.bloaty_report_pb`,
which was the result of running bloaty on the `async-default` library.
This library is extremely small (its report file is less than 1KB),
hence suitable for checking in as golden data.
