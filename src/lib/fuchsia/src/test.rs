// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod test {

    use anyhow::Error;
    use argh::FromArgs;

    // TODO(anp): add or expand tests to ensure logging is initialized correctly.

    #[derive(FromArgs)]
    /// Test component.
    struct Options {
        #[argh(switch)]
        /// test argument that should always be off
        should_be_false: bool,
    }

    #[fuchsia::test]
    fn empty_test() {}

    #[fuchsia::test(logging = false)]
    fn empty_test_without_logging() {}

    #[fuchsia::test(logging = true)]
    fn empty_test_with_logging() {}

    #[fuchsia::test]
    async fn empty_async_test() {}

    #[fuchsia::test(threads = 1)]
    async fn empty_singlethreaded_test() {}

    #[fuchsia::test(threads = 2)]
    async fn empty_multithreaded_test() {}

    #[cfg(target_os = "fuchsia")]
    #[fuchsia::test(allow_stalls)]
    async fn empty_allow_stalls_test() {}

    #[cfg(target_os = "fuchsia")]
    #[fuchsia::test(allow_stalls = false)]
    async fn empty_not_allow_stalls_test() {}

    #[cfg(target_os = "fuchsia")]
    #[fuchsia::test(allow_stalls = true)]
    async fn empty_very_allow_stalls_test() {}

    #[cfg(target_os = "fuchsia")]
    #[fuchsia::test(allow_stalls = true, threads = 1)]
    async fn empty_very_singlethreaded_allow_stalls_test() {}

    #[fuchsia::component]
    #[test]
    fn empty_component_test() {}

    #[fuchsia::component]
    #[test]
    async fn empty_async_component_test() {}

    #[fuchsia::component(threads = 1)]
    #[test]
    async fn empty_async_singlethreaded_component_test() {}

    #[fuchsia::component(threads = 2)]
    #[test]
    async fn empty_async_multithreaded_component_test() {}

    #[fuchsia::test]
    fn empty_test_with_result() -> Result<(), Error> {
        Ok(())
    }

    #[fuchsia::test]
    async fn empty_async_test_with_result() -> Result<(), Error> {
        Ok(())
    }

    #[fuchsia::test(threads = 1)]
    async fn empty_singlethreaded_test_with_result() -> Result<(), Error> {
        Ok(())
    }

    #[fuchsia::test(threads = 2)]
    async fn empty_multithreaded_test_with_result() -> Result<(), Error> {
        Ok(())
    }

    #[cfg(target_os = "fuchsia")]
    #[fuchsia::test(allow_stalls = false)]
    async fn empty_not_allow_stalls_test_with_result() -> Result<(), Error> {
        Ok(())
    }

    #[cfg(target_os = "fuchsia")]
    #[fuchsia::test(allow_stalls = true)]
    async fn empty_allow_stalls_test_with_result() -> Result<(), Error> {
        Ok(())
    }

    #[cfg(target_os = "fuchsia")]
    #[fuchsia::test(allow_stalls = false, threads = 1)]
    async fn empty_very_singlethreaded_not_allow_stalls_test_with_result() -> Result<(), Error> {
        Ok(())
    }

    // We combine #[fuchsia::component] and #[test] here as a kludge to enable testing of the
    // fuchsia::component code in a unit test.
    // Real users of the fuchsia library should not do this, and the ability to do so is not
    // guaranteed to be present in the future.
    #[fuchsia::component]
    #[test]
    fn empty_component_test_with_result() -> Result<(), Error> {
        Ok(())
    }

    // We combine #[fuchsia::component] and #[test] here as a kludge to enable testing of the
    // fuchsia::component code in a unit test.
    // Real users of the fuchsia library should not do this, and the ability to do so is not
    // guaranteed to be present in the future.
    #[fuchsia::component]
    #[test]
    async fn empty_async_component_test_with_result() -> Result<(), Error> {
        Ok(())
    }

    // We combine #[fuchsia::component] and #[test] here as a kludge to enable testing of the
    // fuchsia::component code in a unit test.
    // Real users of the fuchsia library should not do this, and the ability to do so is not
    // guaranteed to be present in the future.
    #[fuchsia::component(threads = 1)]
    #[test]
    async fn empty_async_singlethreaded_component_test_with_result() -> Result<(), Error> {
        Ok(())
    }

    // We combine #[fuchsia::component] and #[test] here as a kludge to enable testing of the
    // fuchsia::component code in a unit test.
    // Real users of the fuchsia library should not do this, and the ability to do so is not
    // guaranteed to be present in the future.
    #[fuchsia::component(threads = 2)]
    #[test]
    async fn empty_async_multithreaded_component_test_with_result() -> Result<(), Error> {
        Ok(())
    }

    // fuchsia::component with arguments can't be written as a test
    // (since argh will parse command line arguments and these will be arguments defining
    // the test execution environment)
    #[allow(dead_code)]
    #[fuchsia::component]
    fn empty_component_test_with_argument(opt: Options) {
        assert_eq!(opt.should_be_false, false);
    }

    // fuchsia::component with arguments can't be written as a test
    // (since argh will parse command line arguments and these will be arguments defining
    // the test execution environment)
    #[allow(dead_code)]
    #[fuchsia::component]
    async fn empty_async_component_test_with_argument(opt: Options) {
        assert_eq!(opt.should_be_false, false);
    }

    // fuchsia::component with arguments can't be written as a test
    // (since argh will parse command line arguments and these will be arguments defining
    // the test execution environment)
    #[allow(dead_code)]
    #[fuchsia::component(threads = 1)]
    async fn empty_async_singlethreaded_component_test_with_argument(opt: Options) {
        assert_eq!(opt.should_be_false, false);
    }

    // fuchsia::component with arguments can't be written as a test
    // (since argh will parse command line arguments and these will be arguments defining
    // the test execution environment)
    #[allow(dead_code)]
    #[fuchsia::component(threads = 2)]
    async fn empty_async_multithreaded_component_test_with_argument(opt: Options) {
        assert_eq!(opt.should_be_false, false);
    }

    // fuchsia::component with arguments can't be written as a test
    // (since argh will parse command line arguments and these will be arguments defining
    // the test execution environment)
    #[allow(dead_code)]
    #[fuchsia::component]
    fn empty_component_test_with_argument_and_result(opt: Options) -> Result<(), Error> {
        assert_eq!(opt.should_be_false, false);
        Ok(())
    }

    // fuchsia::component with arguments can't be written as a test
    // (since argh will parse command line arguments and these will be arguments defining
    // the test execution environment)
    #[allow(dead_code)]
    #[fuchsia::component]
    async fn empty_async_component_test_with_argument_and_result(
        opt: Options,
    ) -> Result<(), Error> {
        assert_eq!(opt.should_be_false, false);
        Ok(())
    }

    // fuchsia::component with arguments can't be written as a test
    // (since argh will parse command line arguments and these will be arguments defining
    // the test execution environment)
    #[allow(dead_code)]
    #[fuchsia::component(threads = 1)]
    async fn empty_async_singlethreaded_component_test_with_argument_and_result(
        opt: Options,
    ) -> Result<(), Error> {
        assert_eq!(opt.should_be_false, false);
        Ok(())
    }

    // fuchsia::component with arguments can't be written as a test
    // (since argh will parse command line arguments and these will be arguments defining
    // the test execution environment)
    #[allow(dead_code)]
    #[fuchsia::component(threads = 2)]
    async fn empty_async_multithreaded_component_test_with_argument_and_result(
        opt: Options,
    ) -> Result<(), Error> {
        assert_eq!(opt.should_be_false, false);
        Ok(())
    }
}
