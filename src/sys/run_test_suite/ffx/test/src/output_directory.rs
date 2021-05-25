// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, std::cmp::Ordering, std::ffi::OsString, std::fs::DirEntry, std::path::PathBuf,
};

/// Manager for a directory used to store latest test results. `DirectoryManager` vends
/// paths to new subdirectories and automatically removes old directories.
pub(crate) struct DirectoryManager {
    root: PathBuf,
    max_entries: usize,
    time_supplier: Box<dyn TimeSupplier>,
}

impl DirectoryManager {
    /// Create a new DirectoryManager to manage directories in `root`. Ensures the
    /// `root` directory exists.
    pub fn new(root: PathBuf, max_entries: usize) -> Result<Self> {
        Self::new_with_time_supplier(root, max_entries, RealTimeSupplier)
    }

    fn new_with_time_supplier<T: TimeSupplier + 'static>(
        root: PathBuf,
        max_entries: usize,
        time_supplier: T,
    ) -> Result<Self> {
        std::fs::DirBuilder::new().recursive(true).create(&root)?;
        Ok(Self { root, max_entries, time_supplier: Box::new(time_supplier) })
    }

    /// Create a new output directory, deleting old directories as necessary.
    pub fn new_directory(&mut self) -> Result<PathBuf> {
        let mut entries = self.entries_ordered()?;
        while entries.len() >= self.max_entries {
            let oldest_entry = entries.pop().unwrap();
            std::fs::remove_dir_all(self.root.join(oldest_entry.name))?;
        }
        let new_directory = self.root.join(timestamp(self.time_supplier.now()));
        std::fs::DirBuilder::new().create(&new_directory)?;
        Ok(new_directory)
    }

    /// Get a path to the most recent run directory, if any exist.
    pub fn latest_directory(&self) -> Result<Option<PathBuf>> {
        let entries = self.entries_ordered()?;
        Ok(entries.into_iter().next().map(|entry| self.root.join(entry.name)))
    }

    /// List entries in order of increasing age.
    fn entries_ordered(&self) -> Result<Vec<RunDirectory>> {
        let mut directories = std::fs::read_dir(&self.root)?
            .filter_map::<RunDirectory, _>(|result| match result {
                Ok(dir_entry) => Some(dir_entry.into()),
                Err(_) => None,
            })
            .collect::<Vec<_>>();

        directories.sort_by(cmp_run_directories_increasing_age);
        Ok(directories)
    }
}

struct RunDirectory {
    /// Name of the directory.
    name: OsString,
    /// Timestamp, if any, parsed from the name.
    timestamp: Option<chrono::DateTime<chrono::Utc>>,
}

impl From<DirEntry> for RunDirectory {
    fn from(dir_entry: DirEntry) -> Self {
        let dir_name = dir_entry.path().iter().last().unwrap().to_os_string();
        Self { timestamp: dir_name.to_str().and_then(|s| parse_timestamp(s)), name: dir_name }
    }
}

fn cmp_run_directories_increasing_age(left: &RunDirectory, right: &RunDirectory) -> Ordering {
    cmp_run_directories(left, right).reverse()
}

fn cmp_run_directories(left: &RunDirectory, right: &RunDirectory) -> Ordering {
    // Prefer to order by timestamp. Entries missing timestamp are 'less' than those with a
    // timestamp, and are otherwise sorted by name.
    match (left.timestamp, right.timestamp) {
        (Some(left_time), Some(right_time)) => left_time.cmp(&right_time),
        (Some(_), None) => Ordering::Greater,
        (None, Some(_)) => Ordering::Less,
        (None, None) => left.name.cmp(&right.name),
    }
}

/// UTC timestamp, down to millis.
const DIRECTORY_TIMESTAMP_FORMAT: &str = "%Y%m%dT%H%M%S%3f";

fn timestamp(time: chrono::DateTime<chrono::Utc>) -> String {
    format!("{}", time.format(DIRECTORY_TIMESTAMP_FORMAT))
}

fn parse_timestamp(timestamp: &str) -> Option<chrono::DateTime<chrono::Utc>> {
    chrono::DateTime::parse_from_str(timestamp, DIRECTORY_TIMESTAMP_FORMAT)
        .map(|datetime| datetime.with_timezone(&chrono::Utc))
        .ok()
}

/// Trait for injecting fake times in tests.
trait TimeSupplier {
    fn now(&self) -> chrono::DateTime<chrono::Utc>;
}

/// Time supplier that reads the real system time.
struct RealTimeSupplier;

impl TimeSupplier for RealTimeSupplier {
    fn now(&self) -> chrono::DateTime<chrono::Utc> {
        chrono::Utc::now()
    }
}

#[cfg(test)]
mod test {
    use {super::*, std::cell::RefCell, std::fs::File, std::io::Write, std::path::Path};

    const MAX_DIR_ENTRIES: usize = 7;

    /// Supplies a test with fake time that is guaranteed to advance for each invocation of now().
    /// This is necessary as our directories are timestamped down to the millisecond. While this is
    /// a high enough resolution for test runs, under test if we rapidly create directories, time
    /// may not have updated and we could get errors with name collisions.
    struct FakeMonotonicTimeSupplier {
        last_time: RefCell<chrono::DateTime<chrono::Utc>>,
    }

    impl TimeSupplier for FakeMonotonicTimeSupplier {
        fn now(&self) -> chrono::DateTime<chrono::Utc> {
            let mut last_time = self.last_time.borrow_mut();
            let new_time = *last_time + chrono::Duration::seconds(1);
            *last_time = new_time;
            new_time
        }
    }

    fn new_manager(root: &Path) -> DirectoryManager {
        DirectoryManager::new_with_time_supplier(
            root.to_path_buf(),
            MAX_DIR_ENTRIES,
            FakeMonotonicTimeSupplier { last_time: RefCell::new(chrono::Utc::now()) },
        )
        .expect("create directory manager")
    }

    #[test]
    fn create_dir() {
        let root = tempfile::tempdir().expect("Create temp dir");
        let mut manager = new_manager(root.path());

        let new_directory = manager.new_directory().expect("create new directory");
        assert!(new_directory.exists());
    }

    #[test]
    fn create_dir_removes_old_dirs() {
        let root = tempfile::tempdir().expect("Create temp dir");
        let mut manager = new_manager(root.path());

        let oldest_directory = manager.new_directory().expect("create new directory");
        assert!(oldest_directory.exists());
        // Add a file. The manager should still be able to clean up the directory.
        let mut file = File::create(oldest_directory.join("file.txt")).expect("create file");
        file.write("hello".as_bytes()).expect("write to file");
        drop(file);

        let subsequent_directories =
            std::iter::repeat_with(|| manager.new_directory().expect("create new directory"))
                .take(MAX_DIR_ENTRIES)
                .collect::<Vec<_>>();
        assert!(!oldest_directory.exists());
        assert!(subsequent_directories.iter().all(|path| path.exists()));
    }

    #[test]
    fn latest_dir() {
        let root = tempfile::tempdir().expect("Create temp dir");
        let mut manager = new_manager(root.path());

        assert_eq!(manager.latest_directory().expect("Get latest directory"), None);

        let first_directory = manager.new_directory().expect("create new directory");
        assert_eq!(
            manager.latest_directory().expect("get latest directory"),
            Some(first_directory)
        );

        let _ = std::iter::repeat_with(|| manager.new_directory().expect("create new directory"))
            .take(MAX_DIR_ENTRIES)
            .collect::<Vec<_>>();

        let new_directory = manager.new_directory().expect("create new directory");
        assert_eq!(manager.latest_directory().expect("get latest directory"), Some(new_directory));
    }
}
