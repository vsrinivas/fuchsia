# test_list: test-list.json definition

This library defines the format of test-list.json. test-list.json describes which tests suites are present in a build, and it provides instructions on how to execute those test cases.

## Schema

The schema for test-list.json is not yet stabilized, however, the below definition serves as a supplement to the code comments in this crate:

```json5
{
    // Identifier for schema version being used.
    //
    // Currently always set to "experimental"
    "schema_id": "experimental",
    // Array containing all tests in the file.
    "data": [
        {
            // The name of the test.
            //
            // Names are unique in the tests array.
            "name": "string",

            // Arbitrary labels for matching.
            //
            // It is common to use build-system-specific identifiers
            // to describe where the test came from.
            "labels": [
                "string",
                "array"
            ],

            // Zero or more key value pairs for tagging tests.
            //
            // Tag keys do not need to be unique.
            "tags": [
                {
                    "key": "string",
                    "value": "string"
                }
            ],

            // Optional description of how to execute the test.
            "execution": {
                // Execution type
                //
                // See execution definitions below for other fields.
                "type": "string"
            }
        }
    ]
}

// fuchsia_component execution definition
{
    // Designates that this test executes as a Fuchsia component.
    "type": "fuchsia_component",

    // A Fuchsia Component URL to execute as a test.
    //
    // This component or its runner must implement the fuchsia.test.Suite FIDL protocol.
    "component_url": "string",

    // Command line arguments to pass to the test.
    "test_args": ["string", "array"],

    // The number of seconds before the test is considered "timed out"
    "timeout_seconds": 30,

    // Optional filters of which test cases to include from the component.
    "test_filters": ["optional", "string", "array"],

    // If true, run tests marked as "disabled" anyway.
    "also_run_disabled_tests": false,

    // Optional maximum number of tests to run in parallel.
    //
    // Can be overridden by runner. If not set, the runner chooses the value.
    "parallel": 4,

    // If set, this is the maximum severity of logs allowed to be
    // written by components in the test suite.
    //
    // This value may be used to make spamming high-severity logs cause
    // a test failure.
    "max_severity_logs": "WARN"
}
```

### Tags

There is no set of tags required to be present in the test list, and each repository is free to choose their own tagging policy and methodology.

For the set of tags used by fuchsia.git builds, see [test_list_tool](/tools/test_list_tool/README.md).