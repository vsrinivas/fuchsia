// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    use fixture::fixture;
    use futures::{SinkExt as _, StreamExt as _};

    fn non_async_setup(test_name: &str, test: impl FnOnce(&str)) {
        test(test_name)
    }

    #[fixture(non_async_setup)]
    #[test]
    fn non_async_test(test_name: &str) {
        assert_eq!(test_name, "non_async_test");
    }

    fn no_args_setup(_test_name: &str, test: impl FnOnce()) {
        test()
    }

    #[fixture(no_args_setup)]
    #[test]
    fn no_args_test() {
        assert_eq!(1 + 1, 2);
    }

    async fn async_setup<F, Fut>(_test_name: &str, test: F)
    where
        F: FnOnce(futures::channel::mpsc::Sender<()>) -> Fut,
        Fut: futures::Future<Output = ()>,
    {
        let (tx, mut rx) = futures::channel::mpsc::channel(0);
        let rx_fut = async {
            assert_eq!(rx.next().await, Some(()));
        };
        let ((), ()) = futures::future::join(rx_fut, test(tx)).await;
    }

    #[fixture(async_setup)]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn async_test(mut tx: futures::channel::mpsc::Sender<()>) {
        let () = tx.send(()).await.unwrap();
    }

    #[derive(Default, Debug, PartialEq)]
    struct Foo;

    fn setup_ref_input(_test_name: &str, test: impl FnOnce(Foo)) {
        test(Foo::default())
    }

    #[fixture(setup_ref_input)]
    #[test]
    fn test_ref_input(ref foo: Foo) {
        assert_eq!(foo, &Foo::default());
    }

    fn generic_setup<T: Default>(_test_name: &str, test: impl FnOnce(T)) {
        test(T::default())
    }

    #[fixture(generic_setup)]
    #[test]
    fn generic_test(foo: Foo) {
        assert_eq!(foo, Foo::default());
    }

    fn setup_with_uninferred_generic<T, const VALUE: usize>(
        _test_name: &str,
        test: impl FnOnce(&str, usize),
    ) {
        test(std::any::type_name::<T>(), VALUE)
    }

    #[fixture(setup_with_uninferred_generic::<u8, 3>)]
    #[test]
    fn uninferred_generic_test(name: &str, value: usize) {
        assert_eq!(name, "u8");
        assert_eq!(value, 3)
    }
}
