// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::channel::oneshot;
use futures::future;
use futures::future::{FutureExt, LocalFutureObj};
use std::collections::{HashMap, HashSet};
use std::mem;

/// Representation of errors sent to the client.
#[derive(PartialEq, Eq, Hash, Debug)]
pub enum CloudError {
    /// The requested object was not found.
    ObjectNotFound(ObjectId),
    /// The requested fingerprint is not present.
    FingerprintNotFound(Fingerprint),
    /// The token is invalid.
    InvalidToken,
    /// Data is malformed.
    ParseError,
}

/// A wrapper for a position in the commit log.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub struct Token(pub usize);

/// A wrapper for the id of a commit.
#[derive(PartialEq, Eq, Hash, Debug, Clone)]
pub struct CommitId(pub Vec<u8>);

/// A wrapper for the id of an object.
#[derive(PartialEq, Eq, Hash, Debug, Clone)]
pub struct ObjectId(pub Vec<u8>);

/// A wrapper for application and page ids.
#[derive(PartialEq, Eq, Hash, Debug, Clone)]
pub struct PageId(pub Vec<u8>, pub Vec<u8>);

/// A wrapper for fingerprints.
#[derive(PartialEq, Eq, Hash, Debug, Clone)]
pub struct Fingerprint(pub Vec<u8>);

/// An object stored in the cloud.
pub struct Object {
    /// The data associated to this object.
    pub data: Vec<u8>,
}

/// A commit stored in the cloud.
pub struct Commit {
    /// The id of this commit.
    pub id: CommitId,
    /// Opaque data associated to the commit.
    pub data: Vec<u8>,
}

/// Stores the set of devices.
pub struct DeviceSet {
    /// The set of fingerprints present on the cloud.
    fingerprints: HashSet<Fingerprint>,
    /// Shared future that is woken up on cloud erasure.
    watch_receiver: future::Shared<oneshot::Receiver<()>>,
    /// Sender for erasure_receiver.
    watch_sender: oneshot::Sender<()>,
}

/// Stores the state of a page.
pub struct PageCloud {
    /// The set of objects uploaded by the clients.
    objects: HashMap<ObjectId, Object>,
    /// The ids of commits uploaded by the clients, stored in upload order.
    commit_log: Vec<CommitId>,
    /// The set of commits uploaded by the clients.
    commits: HashMap<CommitId, Commit>,
    /// Shared future that is woken up on new commits.
    watch_receiver: future::Shared<oneshot::Receiver<()>>,
    /// Sender for watch_receiver.
    watch_sender: oneshot::Sender<()>,
}

/// Stores the state of the cloud.
pub struct Cloud {
    pages: HashMap<PageId, PageCloud>,
    device_set: DeviceSet,
}

pub type PageCloudWatcher = LocalFutureObj<'static, ()>;
impl PageCloud {
    /// Creates a new, empty page.
    pub fn new() -> PageCloud {
        let (watch_sender, watch_receiver) = oneshot::channel();
        PageCloud {
            objects: HashMap::new(),
            commit_log: Vec::new(),
            commits: HashMap::new(),
            watch_sender,
            watch_receiver: watch_receiver.shared(),
        }
    }

    /// Returns the given object, or ObjectNotFound.
    pub fn get_object(&self, id: &ObjectId) -> Result<&Object, CloudError> {
        if let Some(object) = self.objects.get(id) {
            Ok(object)
        } else {
            Err(CloudError::ObjectNotFound(id.clone()))
        }
    }

    /// Adds an object to the cloud. The object may already be
    /// present.
    pub fn add_object(&mut self, id: ObjectId, object: Object) -> Result<(), CloudError> {
        self.objects.insert(id, object);
        Ok(())
    }

    /// Atomically adds a series a commits to the cloud and updates
    /// the commit log. Commits that were already present are not
    /// re-added to the log.
    pub fn add_commits(&mut self, commits: Vec<Commit>) -> Result<(), CloudError> {
        let mut will_insert = Vec::new();

        for commit in commits.iter() {
            if let Some(_existing) = self.commits.get(&commit.id) {
                continue;
            };
            will_insert.push(commit.id.clone())
        }

        // Mutate the data structure.
        self.commit_log.append(&mut will_insert);
        for commit in commits.into_iter() {
            self.commits.insert(commit.id.clone(), commit);
        }

        self.notify_watchers();

        Ok(())
    }

    /// Returns a future that will wake up on new commits, or None if position is not after the latest commit.
    pub fn watch(&self, position: Token) -> Option<PageCloudWatcher> {
        if position.0 < self.commit_log.len() {
            None
        } else {
            // We ignore cancellations, because extra watch notifications are OK.
            Some(LocalFutureObj::new(self.watch_receiver.clone().map(|_| ()).boxed()))
        }
    }

    /// Returns a vector of new commits after position and a new
    /// position.
    pub fn get_commits(&self, position: Token) -> Option<(Token, Vec<&Commit>)> {
        if position.0 >= self.commit_log.len() {
            return None;
        };

        let new_commits = self.commit_log[position.0..]
            .iter()
            .map(|id| self.commits.get(id).expect("Unknown commit in commit log."))
            .collect();
        Some((Token(self.commit_log.len()), new_commits))
    }

    fn notify_watchers(&mut self) {
        let (new_sender, new_receiver) = oneshot::channel();
        let _ = mem::replace(&mut self.watch_sender, new_sender).send(());
        self.watch_receiver = new_receiver.shared();
    }
}

impl Cloud {
    /// Creates a new, empty cloud storage.
    pub fn new() -> Cloud {
        Self { device_set: DeviceSet::new(), pages: HashMap::new() }
    }

    /// Returns the page with the given id. The page is created if
    /// absent.
    pub fn get_page(&mut self, id: PageId) -> &mut PageCloud {
        self.pages.entry(id).or_insert_with(|| PageCloud::new())
    }

    /// Returns the device set.
    pub fn get_device_set(&mut self) -> &mut DeviceSet {
        &mut self.device_set
    }
}

pub type DeviceSetWatcher = LocalFutureObj<'static, ()>;
impl DeviceSet {
    /// Creates a new, empty device set.
    pub fn new() -> DeviceSet {
        let (watch_sender, watch_receiver) = oneshot::channel();
        DeviceSet {
            fingerprints: HashSet::new(),
            watch_sender,
            watch_receiver: watch_receiver.shared(),
        }
    }

    /// Adds a fingerprint to the set. Nothing happens if the
    /// fingerprint is already present.
    pub fn set_fingerprint(&mut self, fingerprint: Fingerprint) {
        self.fingerprints.insert(fingerprint);
    }

    /// Checks that a fingerprint is present in the cloud, or returns
    /// FingerprintNotFound.
    pub fn check_fingerprint(&self, fingerprint: &Fingerprint) -> Result<(), CloudError> {
        if self.fingerprints.contains(fingerprint) {
            Ok(())
        } else {
            Err(CloudError::FingerprintNotFound(fingerprint.clone()))
        }
    }

    /// Erases all fingerprints and calls the watchers.
    pub fn erase(&mut self) {
        self.fingerprints.clear();
        let (new_sender, new_receiver) = oneshot::channel();
        let _ = mem::replace(&mut self.watch_sender, new_sender).send(());
        self.watch_receiver = new_receiver.shared();
    }

    /// If `fingerprint` is present on the cloud, returns a future that
    /// completes when the cloud is erased. Otherwise, returns
    /// `FingerprintNotFound`.
    pub fn watch(&self, fingerprint: &Fingerprint) -> Result<DeviceSetWatcher, CloudError> {
        if !self.fingerprints.contains(fingerprint) {
            return Err(CloudError::FingerprintNotFound(fingerprint.clone()));
        }
        let fut =
            self.watch_receiver.clone().map(|r| r.expect("Storage destroyed before clients?"));
        Ok(LocalFutureObj::new(fut.boxed()))
    }
}
