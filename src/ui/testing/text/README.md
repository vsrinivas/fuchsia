# Text Field Test Suite

This package can be used to test an implementation of TextField's correctness. It currently has a very limited set of tests, and more will need to be added in the future.

## Running the tests

We'd like all TextFields on the operating system to conform to an identical set of behaviors, so that keyboards don't need to worry about varying implementations. If you implement a `TextField` on Fuchsia, during your automated integration tests, you should spin up this package, connect to its `TextFieldTestSuite` interface, and use it to run the tests. For instance, in Rust:

```rs
let launcher = launcher().unwrap();
let app = fuchsia_component::client::launch(
    &launcher,
    "fuchsia-pkg://fuchsia.com/text_test_suite#meta/test_suite.cmx".to_string(),
    None,
)
.unwrap();
let tester = app
    .connect_to_service::<txt_testing::TextFieldTestSuiteMarker>()
    .unwrap();
let mut passed = true;
let test_list = await!(tester.list_tests()).unwrap();
for test in test_list {
    // It's important to generate a new, fresh, empty instance of your TextField for
    // every new test.
    let text_field = my_function_that_gets_new_text_field();
    let (passed, msg) = await!(tester.run_test(text_field, test.id)).unwrap();
    if let Err(e) = await!(run_test(&tester, test.id)) {
        passed = false;
        eprintln!("[ FAIL ] {}\n{}", test.name, e);
    } else {
        eprintln!("[  ok  ] {}", test.name);
    }
}
if !passed {
    panic!("Text integration tests failed");
}
```

## Adding a new test

1. Implement the test function in `tests.rs`, using the helpers in `test_helpers.rs`.
2. Add your new function to the list of tests in the `text_field_tests!` macro in `main.rs`.
3. Run your test against existing implementations to ensure they work as expected.
