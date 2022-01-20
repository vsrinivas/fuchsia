// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple publish-and-subscribe facility.

use {
    fuchsia_syslog::fx_log_info,
    serde::{Deserialize, Serialize},
    std::{
        cell::RefCell,
        collections::BTreeMap,
        fs::{self, File},
        future::Future,
        io,
        path::{Path, PathBuf},
        pin::Pin,
        task::{Context, Poll, Waker},
    },
};

/// A rendezvous point for publishers and subscribers.
pub struct PubSubHub {
    // To minimize the risk of run-time errors, we never store the borrowed `inner` in a named
    // variable. By only borrowing `inner` via temporaries, we make any simultaneous borrows easier
    // to spot in review. And spotting simultaneous borrows enables us to spot conflicting borrows
    // (simultaneous `borrow()` and `borrow_mut()`.)
    inner: RefCell<PubSubHubInner>,
    // The path of the file to load from and write the current value to.
    storage_path: PathBuf,
}

/// The `Future` used by a subscriber to `await` updates.
pub struct PubSubFuture<'a> {
    // See comment for `PubSubHub::inner`, about how to borrow from `hub`.
    hub: &'a RefCell<PubSubHubInner>,
    id: usize,
    last_value: Option<String>,
}

struct PubSubHubInner {
    item: Option<String>,
    next_future_id: usize,
    wakers: BTreeMap<usize, Waker>,
}

impl PubSubHub {
    pub fn new(storage_path: PathBuf) -> Self {
        let initial_value = load_region_code(&storage_path);
        Self {
            inner: RefCell::new(PubSubHubInner {
                item: initial_value,
                next_future_id: 0,
                wakers: BTreeMap::new(),
            }),
            storage_path,
        }
    }

    /// Publishes `new_value`.
    /// * All pending futures are woken.
    /// * Later calls to `watch_for_change()` will be evaluated against `new_value`.
    pub fn publish<S>(&self, new_value: S)
    where
        S: Into<String>,
    {
        let hub = &self.inner;
        let new_value = new_value.into();
        hub.borrow_mut().item = Some(new_value.clone());
        hub.borrow_mut().wakers.values().for_each(|w| w.wake_by_ref());
        hub.borrow_mut().wakers.clear();
        // Store the value that should be loaded at startup.
        write_region_code(new_value, &self.storage_path);
    }

    /// Watches the value stored in this hub, resolving when the
    /// stored value differs from `last_value`.
    pub fn watch_for_change<S>(&self, last_value: Option<S>) -> PubSubFuture<'_>
    where
        S: Into<String>,
    {
        let hub = &self.inner;
        let id = hub.borrow().next_future_id;
        hub.borrow_mut().next_future_id = id.checked_add(1).expect("`id` is impossibly large");
        PubSubFuture { hub, id, last_value: last_value.map(|s| s.into()) }
    }

    pub fn get_value(&self) -> Option<String> {
        let hub = &self.inner;
        hub.borrow().get_value()
    }
}

/// The regulatory region code as a struct, to be used for reading and writing the value as JSON.
#[derive(Debug, Deserialize, Serialize)]
struct RegulatoryRegion {
    region_code: String,
}

// Try to load the stored region code from a file at the specified path. If an error occurs, it
// will not cause a failure because the cache is not necessary.
// TODO(67860) Add metric for failures reading cache.
fn load_region_code(path: impl AsRef<Path>) -> Option<String> {
    let file = match File::open(path.as_ref()) {
        Ok(file) => file,
        Err(e) => match e.kind() {
            io::ErrorKind::NotFound => return None,
            _ => {
                fx_log_info!(
                    "Failed to read cached regulatory region, will initialize with none: {}",
                    e
                );
                try_delete_file(path);
                return None;
            }
        },
    };
    match serde_json::from_reader::<_, RegulatoryRegion>(io::BufReader::new(file)) {
        Ok(region) => Some(region.region_code),
        Err(e) => {
            fx_log_info!("Error parsing stored regulatory region code: {}", e);
            try_delete_file(path);
            None
        }
    }
}

/// Try to write the region code as a JSON at the specified file location. For example, the file
/// contents may look like "{"region_code": "US"}". Errors saving the region code will not cause a
/// failure because the cache is not necessary.
// TODO(67860) Add metric for failures writing cache.
fn write_region_code(region_code: String, storage_path: impl AsRef<Path>) {
    let write_val = RegulatoryRegion { region_code };
    let file = match File::create(storage_path.as_ref()) {
        Ok(file) => file,
        Err(e) => {
            fx_log_info!("Failed to open file to write regulatory region: {}", e);
            try_delete_file(storage_path);
            return;
        }
    };
    if let Err(e) = serde_json::to_writer(io::BufWriter::new(file), &write_val) {
        fx_log_info!("Failed to write regulatory region: {}", e);
        try_delete_file(storage_path);
    }
}

fn try_delete_file(storage_path: impl AsRef<Path>) {
    if let Err(e) = fs::remove_file(&storage_path) {
        fx_log_info!("Failed to delete previously cached regulatory region: {}", e);
    }
}

impl Future for PubSubFuture<'_> {
    type Output = Option<String>;

    fn poll(self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Self::Output> {
        let hub = &self.hub;
        if hub.borrow().has_value(&self.last_value) {
            hub.borrow_mut().set_waker_for_future(self.id, context.waker().clone());
            Poll::Pending
        } else {
            Poll::Ready(hub.borrow().get_value())
        }
    }
}

impl PubSubHubInner {
    fn set_waker_for_future(&mut self, future_id: usize, waker: Waker) {
        self.wakers.insert(future_id, waker);
    }

    fn has_value(&self, expected: &Option<String>) -> bool {
        self.item == *expected
    }

    fn get_value(&self) -> Option<String> {
        self.item.clone()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        futures_test::task::new_count_waker,
        std::{
            future::Future,
            io::Write,
            pin::Pin,
            task::{Context, Poll},
        },
        tempfile::TempDir,
    };

    #[fasync::run_until_stalled(test)]
    async fn watch_for_change_future_is_pending_when_both_values_are_none() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));
        assert_eq!(0, count.get());
    }

    #[fasync::run_until_stalled(test)]
    async fn watch_for_change_future_is_pending_when_values_are_same_and_not_none() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        hub.publish("US");

        let mut future = hub.watch_for_change(Some("US"));
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));
        assert_eq!(0, count.get());
    }

    #[fasync::run_until_stalled(test)]
    async fn watch_for_change_future_is_immediately_ready_when_argument_differs_from_published_value(
    ) {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        hub.publish("US");

        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Ready(Some("US".to_string())), Pin::new(&mut future).poll(&mut context));
        assert_eq!(0, count.get());
    }

    #[fasync::run_until_stalled(test)]
    async fn single_watcher_is_woken_correctly_on_change_from_none_to_some() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));

        // Change value, and expect wake, and new value.
        hub.publish("US");
        assert_eq!(1, count.get());
        assert_eq!(Poll::Ready(Some("US".to_string())), Pin::new(&mut future).poll(&mut context));
    }

    #[fasync::run_until_stalled(test)]
    async fn single_watcher_is_woken_correctly_on_change_from_some_to_new_some() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        hub.publish("US");

        let mut future = hub.watch_for_change(Some("US"));
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));

        // Change value, and expect wake, and new value.
        hub.publish("SU");
        assert_eq!(1, count.get());
        assert_eq!(Poll::Ready(Some("SU".to_string())), Pin::new(&mut future).poll(&mut context));
    }

    #[fasync::run_until_stalled(test)]
    async fn multiple_watchers_are_woken_correctly_on_change_from_some_to_new_some() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (waker_a, wake_count_a) = new_count_waker();
        let (waker_b, wake_count_b) = new_count_waker();
        let mut context_a = Context::from_waker(&waker_a);
        let mut context_b = Context::from_waker(&waker_b);
        hub.publish("US");

        let mut future_a = hub.watch_for_change(Some("US"));
        let mut future_b = hub.watch_for_change(Some("US"));
        assert_eq!(Poll::Pending, Pin::new(&mut future_a).poll(&mut context_a), "for future a");
        assert_eq!(Poll::Pending, Pin::new(&mut future_b).poll(&mut context_b), "for future b");

        // Change value, and expect wakes, and new value for both futures.
        hub.publish("SU");
        assert_eq!(1, wake_count_a.get(), "for waker a");
        assert_eq!(1, wake_count_b.get(), "for waker b");
        assert_eq!(
            Poll::Ready(Some("SU".to_string())),
            Pin::new(&mut future_a).poll(&mut context_a),
            "for future a"
        );
        assert_eq!(
            Poll::Ready(Some("SU".to_string())),
            Pin::new(&mut future_b).poll(&mut context_b),
            "for future b"
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn multiple_watchers_are_woken_correctly_after_spurious_update() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (waker_a, wake_count_a) = new_count_waker();
        let (waker_b, wake_count_b) = new_count_waker();
        let mut context_a = Context::from_waker(&waker_a);
        let mut context_b = Context::from_waker(&waker_b);
        hub.publish("US");

        let mut future_a = hub.watch_for_change(Some("US"));
        let mut future_b = hub.watch_for_change(Some("US"));
        assert_eq!(Poll::Pending, Pin::new(&mut future_a).poll(&mut context_a), "for future a");
        assert_eq!(Poll::Pending, Pin::new(&mut future_b).poll(&mut context_b), "for future b");

        // Generate spurious update.
        hub.publish("US");
        assert_eq!(Poll::Pending, Pin::new(&mut future_a).poll(&mut context_a), "for future a");
        assert_eq!(Poll::Pending, Pin::new(&mut future_b).poll(&mut context_b), "for future b");

        // Generate a real update. Expect wakes, and new value for both futures.
        let old_wake_count_a = wake_count_a.get();
        let old_wake_count_b = wake_count_b.get();
        hub.publish("SU");
        assert_eq!(1, wake_count_a.get() - old_wake_count_a);
        assert_eq!(1, wake_count_b.get() - old_wake_count_b);
        assert_eq!(
            Poll::Ready(Some("SU".to_string())),
            Pin::new(&mut future_a).poll(&mut context_a),
            "for future a"
        );
        assert_eq!(
            Poll::Ready(Some("SU".to_string())),
            Pin::new(&mut future_b).poll(&mut context_b),
            "for future b"
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn multiple_watchers_can_share_a_waker() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        let mut future_a = hub.watch_for_change(Option::<String>::None);
        let mut future_b = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future_a).poll(&mut context), "for future a");
        assert_eq!(Poll::Pending, Pin::new(&mut future_b).poll(&mut context), "for future b");

        // Change value, and expect wakes, and new value for both futures.
        hub.publish("US");
        assert_eq!(2, count.get());
        assert_eq!(
            Poll::Ready(Some("US".to_string())),
            Pin::new(&mut future_a).poll(&mut context),
            "for future a"
        );
        assert_eq!(
            Poll::Ready(Some("US".to_string())),
            Pin::new(&mut future_b).poll(&mut context),
            "for future b"
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn single_watcher_is_not_woken_again_after_future_is_ready() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));

        // Publish an update, which resolves `future`.
        hub.publish("US");
        assert_eq!(1, count.get());
        assert_eq!(Poll::Ready(Some("US".to_string())), Pin::new(&mut future).poll(&mut context));

        // Further updates should leave `count` unchanged, since they should not wake `waker`.
        hub.publish("SU");
        assert_eq!(1, count.get());
    }

    #[fasync::run_until_stalled(test)]
    async fn second_watcher_is_woken_for_second_update() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));

        // Publish first update, which resolves `future`.
        hub.publish("US");
        assert_eq!(1, count.get());
        assert_eq!(Poll::Ready(Some("US".to_string())), Pin::new(&mut future).poll(&mut context));

        // Create a new `future`, and verify that a second update resolves the new `future`.
        let mut future = hub.watch_for_change(Some("US"));
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));
        hub.publish("SU");
        assert!(count.get() > 1, "Count should be >1, but is {}", count.get());
        assert_eq!(Poll::Ready(Some("SU".to_string())), Pin::new(&mut future).poll(&mut context));
    }

    #[fasync::run_until_stalled(test)]
    async fn multiple_polls_of_single_watcher_do_not_cause_multiple_wakes_when_waker_is_reused() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));

        // Publish an update, which resolves `future`.
        hub.publish("US");
        assert_eq!(1, count.get());
    }

    #[fasync::run_until_stalled(test)]
    async fn multiple_polls_of_single_watcher_do_not_cause_multiple_wakes_when_waker_is_replaced() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        let (waker_a, wake_count_a) = new_count_waker();
        let (waker_b, wake_count_b) = new_count_waker();
        let mut context_a = Context::from_waker(&waker_a);
        let mut context_b = Context::from_waker(&waker_b);
        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context_a));
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context_b));

        // Publish an update, which resolves `future`.
        hub.publish("US");
        assert_eq!(0, wake_count_a.get());
        assert_eq!(1, wake_count_b.get());
    }

    #[test]
    fn get_value_is_none() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        assert_eq!(None, hub.get_value());
    }

    #[test]
    fn get_value_is_some() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path);
        hub.publish("US");
        assert_eq!(Some("US".to_string()), hub.get_value());
    }

    #[test]
    fn published_value_is_saved_and_loaded_on_creation() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        let hub = PubSubHub::new(path.to_path_buf());
        assert_eq!(hub.get_value(), None);
        hub.publish("WW");
        assert_eq!(hub.get_value(), Some("WW".to_string()));

        // Create a new PubSubHub with the same storage path and verify that the initial value is
        // the last thing published to previous PubSubHub.
        let hub = PubSubHub::new(path.to_path_buf());
        assert_eq!(hub.get_value(), Some("WW".to_string()));

        // Verify that the files is unaffected.
        let file = File::open(&path).expect("Failed to open file");
        assert_matches!(
            serde_json::from_reader(io::BufReader::new(file)),
            Ok(RegulatoryRegion{ region_code }) if region_code.as_str() == "WW"
        );
    }

    #[test]
    fn publishing_over_previously_saved_value_overwrites_cache() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");

        // Write some value to the cache.
        let cache_val = RegulatoryRegion { region_code: "WW".to_string() };
        let file = File::create(&path).expect("failed to create file");
        serde_json::to_writer(io::BufWriter::new(file), &cache_val)
            .expect("Failed to write JSON to file");

        // Check that PubSubHub loads the correct value.
        let hub = PubSubHub::new(path.to_path_buf());
        assert_eq!(hub.get_value(), Some("WW".to_string()));

        // Publish a new value and check that the file has the new value.
        hub.publish("US");
        let file = File::open(&path).expect("Failed to open file");
        assert_matches!(
            serde_json::from_reader(io::BufReader::new(file)),
            Ok(RegulatoryRegion{ region_code }) if region_code.as_str() == "US"
        );
        let hub = PubSubHub::new(path.to_path_buf());
        assert_eq!(hub.get_value(), Some("US".to_string()));
    }

    #[test]
    fn load_as_none_if_cache_file_is_bad() {
        let temp_dir = TempDir::new_in("/cache/").expect("failed to create temporary directory");
        let path = temp_dir.path().join("regulatory_region.json");
        assert!(!path.exists());
        let mut file = File::create(&path).expect("failed to create file");
        let bad_contents = b"{\"region_code\": ";
        file.write(bad_contents).expect("failed to write to file");
        file.flush().expect("failed to flush file");

        // Check that PubSubHub is initialized with an unset value.
        let hub = PubSubHub::new(path.to_path_buf());
        assert_eq!(hub.get_value(), None);

        // Check that the bad file was deleted.
        assert_matches!(File::open(&path), Err(io_err) if io_err.kind() == io::ErrorKind::NotFound);
    }
}
