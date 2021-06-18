// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde::{Deserialize, Serialize},
    std::cmp::Ordering,
    std::collections::HashMap,
    std::ffi::OsString,
    std::fs::{DirEntry, File},
    std::path::PathBuf,
    thiserror::Error,
};

/// Manager for a directory used to store test results. `DirectoryManager` vends
/// paths to new subdirectories and automatically removes old directories.
///
/// `DirectoryManager` maintains two lists of subdirectories, a list of "unsaved"
/// subdirectories and a list of "saved" subdirectories.
/// Unsaved subdirectories are conceptually in a bounded queue indexed in order of
/// increasing age. When the queue is full the oldest subdirectory is deleted.
/// Saved subdirectories are identified by name and are not subject to automatic
/// deletion, although they may be deleted manually.
/// New subdirectories are unsaved by default and may be saved with |save_directory|.
pub(crate) struct DirectoryManager {
    root: PathBuf,
    max_entries: usize,
    time_supplier: Box<dyn TimeSupplier>,
    // Mapping from directory name to saved name.
    saved_directories: HashMap<OsString, String>,
}

#[derive(Debug, PartialEq, Eq)]
pub(crate) enum DirectoryId {
    Index(u32),
    Name(String),
}

#[derive(Error, Debug)]
pub(crate) enum DirectoryError {
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),

    #[error("error serializing to disk: {0}")]
    Serializer(#[from] serde_json::error::Error),

    #[error("directory matching the id does not exist")]
    IdNotFound(DirectoryId),

    #[error("another directory is already saved with name '{0}")]
    NameInUse(String),
}

const SAVED_DIRECTORIES_FILE: &str = "saved_directories.json";

type Result<T> = std::result::Result<T, DirectoryError>;

impl DirectoryManager {
    /// Create a new DirectoryManager to manage directories in |root|. Ensures the
    /// |root| directory exists.
    pub fn new(root: PathBuf, max_entries: usize) -> Result<Self> {
        Self::new_with_time_supplier(root, max_entries, RealTimeSupplier)
    }

    fn new_with_time_supplier<T: TimeSupplier + 'static>(
        root: PathBuf,
        max_entries: usize,
        time_supplier: T,
    ) -> Result<Self> {
        std::fs::DirBuilder::new().recursive(true).create(&root)?;
        let saved_directories = read_saved_directories(&root).unwrap_or(HashMap::new());
        Ok(Self { root, max_entries, time_supplier: Box::new(time_supplier), saved_directories })
    }

    /// Create a new output directory, deleting old directories as necessary.
    pub fn new_directory(&mut self) -> Result<PathBuf> {
        let mut unsaved_entries = self
            .entries_ordered()?
            .into_iter()
            .filter(|(_, dir)| !self.saved_directories.contains_key(&dir.dir_name))
            .collect::<Vec<_>>();
        while unsaved_entries.len() >= self.max_entries {
            let (_id, oldest_entry) = unsaved_entries.pop().unwrap();
            std::fs::remove_dir_all(self.root.join(oldest_entry.dir_name))?;
        }
        let new_directory = self.root.join(timestamp(self.time_supplier.now()));
        std::fs::DirBuilder::new().create(&new_directory)?;
        Ok(new_directory)
    }

    /// Get the id and path to the most recent run directory, if any exist.
    pub fn latest_directory(&self) -> Result<Option<PathBuf>> {
        let entries = self.entries_ordered()?;
        Ok(entries.into_iter().next().map(|(_, dir)| self.root.join(dir.dir_name)))
    }

    /// Get a path to the directory matching the given |id|, if one exists.
    pub fn get_by_id(&self, id: DirectoryId) -> Result<Option<PathBuf>> {
        Ok(self
            .entries_ordered()?
            .into_iter()
            .find(|entry| entry.0 == id)
            .map(|(_, entry)| self.root.join(entry.dir_name)))
    }

    /// List present directories in order of increasing age.
    pub fn entries_ordered(&self) -> Result<Vec<(DirectoryId, RunDirectory)>> {
        let mut directories = std::fs::read_dir(&self.root)?
            .filter_map::<RunDirectory, _>(|result| match result {
                Ok(dir_entry) if dir_entry.file_name() == SAVED_DIRECTORIES_FILE => None,
                Ok(dir_entry) => Some(dir_entry.into()),
                Err(_) => None,
            })
            .collect::<Vec<_>>();

        directories.sort_by(cmp_run_directories_increasing_age);
        let mut current_index = 0;
        Ok(directories
            .into_iter()
            .map(|entry| match self.saved_directories.get(&entry.dir_name) {
                Some(save_name) => (DirectoryId::Name(save_name.clone()), entry),
                None => {
                    let index = current_index;
                    current_index += 1;
                    (DirectoryId::Index(index), entry)
                }
            })
            .collect())
    }

    /// Mark the unsaved directory at |index| as saved and assign it the name |save_name|.
    pub fn save_directory(&mut self, index: u32, save_name: String) -> Result<()> {
        if self.saved_directories.values().any(|saved_name| *saved_name == save_name) {
            return Err(DirectoryError::NameInUse(save_name));
        }

        let entries = self.entries_ordered()?;
        let (_id, directory) = entries
            .into_iter()
            .find(|(id, _)| *id == DirectoryId::Index(index))
            .ok_or_else(|| DirectoryError::IdNotFound(DirectoryId::Index(index)))?;

        self.saved_directories.insert(directory.dir_name, save_name);
        write_saved_directories(&self.root, &self.saved_directories)
    }

    /// Manually delete the directory referenced by |id|.
    pub fn delete(&mut self, id: DirectoryId) -> Result<()> {
        let entries = self.entries_ordered()?;
        match entries.into_iter().find(|entry| entry.0 == id) {
            Some((_, directory)) => {
                std::fs::remove_dir_all(self.root.join(&directory.dir_name))?;
                if self.saved_directories.remove(&directory.dir_name).is_some() {
                    // update list of saved directories if the deleted entry was saved
                    write_saved_directories(&self.root, &self.saved_directories)?;
                }
                Ok(())
            }
            None => Err(DirectoryError::IdNotFound(id)),
        }
    }
}

pub struct RunDirectory {
    /// Name of the directory.
    pub dir_name: OsString,
    /// Timestamp, if any, parsed from the name.
    pub timestamp: Option<chrono::DateTime<chrono::Utc>>,
}

impl From<DirEntry> for RunDirectory {
    fn from(dir_entry: DirEntry) -> Self {
        let dir_name = dir_entry.path().iter().last().unwrap().to_os_string();
        Self { timestamp: dir_name.to_str().and_then(|s| parse_timestamp(s)), dir_name }
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
        (None, None) => left.dir_name.cmp(&right.dir_name),
    }
}

// serde won't serialize a HashMap with OsString as a key, so we serialize as a vector instead.
#[derive(Serialize, Deserialize)]
struct SerializableSavedDirectory {
    dir_name: OsString,
    saved_name: String,
}

fn read_saved_directories(root: &PathBuf) -> Result<HashMap<OsString, String>> {
    let dir_file = File::open(root.join(SAVED_DIRECTORIES_FILE))?;
    let saved_list: Vec<SerializableSavedDirectory> = serde_json::from_reader(dir_file)?;

    Ok(saved_list
        .into_iter()
        .map(|serializable| (serializable.dir_name, serializable.saved_name))
        .collect())
}

fn write_saved_directories(
    root: &PathBuf,
    saved_directories: &HashMap<OsString, String>,
) -> Result<()> {
    let saved_list: Vec<SerializableSavedDirectory> = saved_directories
        .iter()
        .map(|(dir_name, saved_name)| SerializableSavedDirectory {
            dir_name: dir_name.clone(),
            saved_name: saved_name.clone(),
        })
        .collect();
    let mut dir_file = File::create(root.join(SAVED_DIRECTORIES_FILE))?;
    Ok(serde_json::to_writer_pretty(&mut dir_file, &saved_list)?)
}

/// UTC timestamp, down to millis.
const DIRECTORY_TIMESTAMP_FORMAT: &str = "%Y%m%dT%H%M%S%3f";

fn timestamp(time: chrono::DateTime<chrono::Utc>) -> String {
    format!("{}", time.format(DIRECTORY_TIMESTAMP_FORMAT))
}

fn parse_timestamp(timestamp: &str) -> Option<chrono::DateTime<chrono::Utc>> {
    let naive_time =
        chrono::NaiveDateTime::parse_from_str(timestamp, DIRECTORY_TIMESTAMP_FORMAT).ok()?;
    Some(chrono::DateTime::from_utc(naive_time, chrono::Utc))
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
    use {
        super::*, std::cell::RefCell, std::convert::TryInto, std::fs::File, std::io::Write,
        std::path::Path,
    };

    const MAX_DIR_ENTRIES: usize = 7;
    const SAVE_NAME: &str = "an interesting test run";

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

    fn test_with_three_directories<F>(test_fn: F)
    where
        F: Fn([PathBuf; 3], DirectoryManager) -> (),
    {
        let root = tempfile::tempdir().expect("Create temp dir");
        let mut manager = new_manager(root.path());
        let mut directories =
            std::iter::repeat_with(|| manager.new_directory().expect("create new directory"))
                .take(3)
                .collect::<Vec<_>>();
        directories.reverse();
        let directories: [PathBuf; 3] = directories.try_into().unwrap();
        for (index, directory) in directories.iter().enumerate() {
            assert_eq!(
                manager.get_by_id(DirectoryId::Index(index as u32)).expect("get by id"),
                Some(directory.clone())
            );
            assert!(directory.exists());
        }
        test_fn(directories, manager);
    }

    #[test]
    fn save_directories_updates_indices() {
        test_with_three_directories(|directories, mut manager| {
            // saving the most recent directory moves ids of earlier directories up
            manager.save_directory(0, SAVE_NAME.to_string()).expect("save directory");
            assert_eq!(
                manager.get_by_id(DirectoryId::Name(SAVE_NAME.to_string())).expect("get by id"),
                Some(directories[0].clone())
            );
            assert_eq!(
                manager.get_by_id(DirectoryId::Index(0)).expect("get by id"),
                Some(directories[1].clone())
            );
            assert_eq!(
                manager.get_by_id(DirectoryId::Index(1)).expect("get by id"),
                Some(directories[2].clone())
            );
        });

        test_with_three_directories(|directories, mut manager| {
            // saving the middle directory moves id of earliest directory
            manager.save_directory(1, SAVE_NAME.to_string()).expect("save directory");
            assert_eq!(
                manager.get_by_id(DirectoryId::Index(0)).expect("get by id"),
                Some(directories[0].clone())
            );
            assert_eq!(
                manager.get_by_id(DirectoryId::Name(SAVE_NAME.to_string())).expect("get by id"),
                Some(directories[1].clone())
            );
            assert_eq!(
                manager.get_by_id(DirectoryId::Index(1)).expect("get by id"),
                Some(directories[2].clone())
            );
        });

        test_with_three_directories(|directories, mut manager| {
            // saving the oldest directory does not affect ids of earlier directories
            manager.save_directory(2, SAVE_NAME.to_string()).expect("save directory");
            assert_eq!(
                manager.get_by_id(DirectoryId::Index(0)).expect("get by id"),
                Some(directories[0].clone())
            );
            assert_eq!(
                manager.get_by_id(DirectoryId::Index(1)).expect("get by id"),
                Some(directories[1].clone())
            );
            assert_eq!(
                manager.get_by_id(DirectoryId::Name(SAVE_NAME.to_string())).expect("get by id"),
                Some(directories[2].clone())
            );
        });
    }

    #[test]
    fn saved_dir_list_persisted() {
        const SAVE_NAME_1: &str = "save-1";
        const SAVE_NAME_2: &str = "save-2";

        let root = tempfile::tempdir().expect("Create temp dir");
        let mut manager = new_manager(root.path());

        let saved_directory_1 = manager.new_directory().expect("create new directory");
        assert!(saved_directory_1.exists());
        manager.save_directory(0, SAVE_NAME_1.to_string()).expect("save directory");

        let saved_directory_2 = manager.new_directory().expect("create new directory");
        let mut unsaved_directories =
            std::iter::repeat_with(|| manager.new_directory().expect("create new directory"))
                .take(MAX_DIR_ENTRIES - 1)
                .collect::<Vec<_>>();
        unsaved_directories.reverse();
        manager
            .save_directory((MAX_DIR_ENTRIES - 1) as u32, SAVE_NAME_2.to_string())
            .expect("save directory");

        drop(manager);

        // A new manager on the same directory should have the same list of saved directories
        let new_manager = new_manager(root.path());
        assert_eq!(
            new_manager.get_by_id(DirectoryId::Name(SAVE_NAME_1.to_string())).expect("get by id"),
            Some(saved_directory_1.clone())
        );
        assert!(saved_directory_1.exists());
        assert_eq!(
            new_manager.get_by_id(DirectoryId::Name(SAVE_NAME_2.to_string())).expect("get by id"),
            Some(saved_directory_2.clone())
        );
        assert!(saved_directory_2.exists());
        for (index, directory) in unsaved_directories.into_iter().enumerate() {
            assert_eq!(
                new_manager.get_by_id(DirectoryId::Index(index as u32)).expect("get by id"),
                Some(directory.clone())
            );
            assert!(directory.exists());
        }
    }

    #[test]
    fn saved_dir_is_not_removed() {
        let root = tempfile::tempdir().expect("Create temp dir");
        let mut manager = new_manager(root.path());

        let saved_directory = manager.new_directory().expect("create new directory");
        assert!(saved_directory.exists());
        manager.save_directory(0, SAVE_NAME.to_string()).expect("save directory");

        let subsequent_directories =
            std::iter::repeat_with(|| manager.new_directory().expect("create new directory"))
                .take(MAX_DIR_ENTRIES)
                .collect::<Vec<_>>();
        // all the directories should still exist.
        assert!(saved_directory.exists());
        assert!(subsequent_directories.iter().all(|path| path.exists()));
        // after creating one more, the saved directory still exists, but one of the subsequent
        // directories is cleaned.
        let newest_directory = manager.new_directory().expect("create new directory");
        assert!(newest_directory.exists());
        assert!(saved_directory.exists());
        assert_eq!(
            subsequent_directories.iter().filter(|path| path.exists()).count(),
            MAX_DIR_ENTRIES - 1,
        );
    }

    #[test]
    fn get_by_id() {
        let root = tempfile::tempdir().expect("Create temp dir");
        let mut manager = new_manager(root.path());

        assert_eq!(manager.get_by_id(DirectoryId::Index(0)).expect("get by id"), None);
        assert_eq!(
            manager.get_by_id(DirectoryId::Name(SAVE_NAME.to_string())).expect("get by id"),
            None
        );

        let mut directories =
            std::iter::repeat_with(|| manager.new_directory().expect("create new directory"))
                .take(MAX_DIR_ENTRIES)
                .collect::<Vec<_>>();
        directories.reverse();

        for (index, directory) in directories.iter().enumerate() {
            assert!(directory.exists());
            assert_eq!(
                manager.get_by_id(DirectoryId::Index(index as u32)).expect("get by id"),
                Some(directory.clone())
            );
        }

        let newest_directory = directories.remove(0);
        manager.save_directory(0, SAVE_NAME.to_string()).expect("Save directory");
        assert_eq!(
            manager.get_by_id(DirectoryId::Name(SAVE_NAME.to_string())).expect("get by id"),
            Some(newest_directory)
        );

        // Since the newest directory was assigned a name, the older directories move up an index
        for (index, directory) in directories.iter().enumerate() {
            assert!(directory.exists());
            assert_eq!(
                manager.get_by_id(DirectoryId::Index(index as u32)).expect("get by id"),
                Some(directory.clone())
            );
        }
    }

    #[test]
    fn delete_dir() {
        let root = tempfile::tempdir().expect("Create temp dir");
        let mut manager = new_manager(root.path());

        let saved_directory = manager.new_directory().expect("create new directory");
        manager.save_directory(0, SAVE_NAME.to_string()).expect("save directory");
        let unsaved_directory = manager.new_directory().expect("create new directory");

        assert!(saved_directory.exists());
        assert!(unsaved_directory.exists());

        manager.delete(DirectoryId::Name(SAVE_NAME.to_string())).expect("delete directory");
        assert!(!saved_directory.exists());
        assert!(unsaved_directory.exists());

        manager.delete(DirectoryId::Index(0)).expect("delete directory");
        assert!(!saved_directory.exists());
        assert!(!unsaved_directory.exists());
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
        assert_eq!(
            manager.latest_directory().expect("get latest directory"),
            Some(new_directory.clone())
        );

        // after saving the latest directory it's still returned as latest.
        manager.save_directory(0, SAVE_NAME.to_string()).expect("save directory");
        assert_eq!(manager.latest_directory().expect("get latest directory"), Some(new_directory));
    }

    #[test]
    fn timestamp_round_trip() {
        let now = RealTimeSupplier.now();
        let timestamp = timestamp(now);
        let parsed = parse_timestamp(&timestamp).expect("parse timestamp");

        // Timestamp is slightly lossy
        let diff = parsed - now;
        assert!(diff >= chrono::Duration::milliseconds(-1));
        assert!(diff <= chrono::Duration::milliseconds(1));
    }
}
