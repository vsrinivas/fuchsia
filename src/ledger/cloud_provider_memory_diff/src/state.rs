// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::*,
    crate::types::*,
    crate::utils::{Signal, SignalWatcher},
    fidl_fuchsia_ledger_cloud as cloud,
    std::collections::{hash_map, HashMap, HashSet},
};

/// An object stored in the cloud.
pub struct Object {
    /// The data associated to this object.
    pub data: Vec<u8>,
}

/// The structure used to store the diffs uploaded by clients.
///
/// When a new commit is added with an associated diff, it is added to the diff tree with the base
/// state of the diff as its parent. Each diff in the diff tree is stored in a DiffTreeEntry, which
/// contains the parent state, the diff, and an origin and depth:
///  - for a diff entry that uses a base commit for which we don't already have a diff entry, the
///  origin is the base commit and the depth is 1
///  - for a subsequent diff entry that uses a base commit for which we already have a diff entry,
///  the origin is *the origin of the base commit diff entry* and depth is the depth of the base
///  commit diff entry + 1
/// We consider that all states that are not present (ie.  have no associated diffs) have themselves
/// as their origins, and depth 0.
///
/// When we remove compatibility with non-diff Ledgers, the origin of all commits will be the empty
/// page state.
// TODO(ambre): remove origin when it is not needed anymore.
struct DiffTreeEntry {
    /// Depth of the entry in the tree: 0 for states that don't have diff, or the depth of the base
    /// plus one.
    depth: usize,
    /// The page state obtained by following base states from this commit.
    origin: PageState,
    /// A diff describing this commit.
    diff: Diff,
}

type DiffTree = HashMap<CommitId, DiffTreeEntry>;

/// A hashtable-based diff accumulator, with compaction.
// Entries are indexed by entry id. An insertion and a deletion with the same entry id cancel each
// other.
struct CompactingAccumulator {
    diff_entries: HashMap<Vec<u8>, DiffEntry>,
}

impl CompactingAccumulator {
    // Creates a new accumulator.
    fn new() -> Self {
        Self { diff_entries: HashMap::new() }
    }

    // Adds an entry to the accumulator.
    fn push(&mut self, diff_entry: DiffEntry) {
        match self.diff_entries.entry(diff_entry.entry_id.clone()) {
            hash_map::Entry::Vacant(e) => {
                e.insert(diff_entry);
            }
            hash_map::Entry::Occupied(e) => {
                // There should never be two entries with the same entry id in a state.  We are
                // either cancelling an insertion with a deletion, or a deletion with an insertion.
                let old_diff_entry = e.remove();
                // There is no good way to report that we have duplicate entries because we detect
                // it too late. Abort so the failure is noticed.
                assert!(
                    old_diff_entry.operation != diff_entry.operation,
                    "invalid diff: entry inserted or deleted twice"
                );
            }
        }
    }

    // Consumes the accumulator and returns a compacted diff equivalent to the list of inserted
    // entries.
    fn done(self) -> Vec<DiffEntry> {
        self.diff_entries.into_iter().map(|(_, e)| e).collect()
    }
}

pub trait OperationExt {
    fn invert(self) -> Self;
}

impl OperationExt for cloud::Operation {
    fn invert(self) -> Self {
        match self {
            cloud::Operation::Insertion => cloud::Operation::Deletion,
            cloud::Operation::Deletion => cloud::Operation::Insertion,
        }
    }
}

impl DiffEntry {
    /// Inverts a DiffEntry: changes insertions to deletions and conversely.
    fn invert(mut self) -> Self {
        self.operation = self.operation.invert();
        self
    }
}

/// Stores the set of devices.
pub struct DeviceSet {
    /// The set of fingerprints present on the cloud.
    fingerprints: HashSet<Fingerprint>,
    /// Signal for cloud erasure.
    erasure_signal: Signal,
}

/// Stores the state of a page.
pub struct PageCloud {
    /// The set of objects uploaded by the clients.
    objects: HashMap<ObjectId, Object>,
    /// The ids of commits uploaded by the clients, stored in upload order.
    commit_log: Vec<CommitId>,
    /// The set of commits uploaded by the clients.
    commits: HashMap<CommitId, Commit>,
    /// The set of diffs uploaded by the clients.
    diffs: DiffTree,
    /// Signal for new commits. Asserting it marks the futures waiting on a new commit for wakeup,
    /// but they don't run until control returns to the executor.
    commit_signal: Signal,
}

/// Stores the state of the cloud.
pub struct Cloud {
    pages: HashMap<PageId, PageCloud>,
    device_set: DeviceSet,
}

pub type PageCloudWatcher = SignalWatcher;
impl PageCloud {
    /// Creates a new, empty page.
    pub fn new() -> PageCloud {
        PageCloud {
            objects: HashMap::new(),
            commit_log: Vec::new(),
            commits: HashMap::new(),
            diffs: HashMap::new(),
            commit_signal: Signal::new(),
        }
    }

    /// Returns the given object, or ObjectNotFound.
    pub fn get_object(&self, id: &ObjectId) -> Result<&Object, ClientError> {
        if let Some(object) = self.objects.get(id) {
            Ok(object)
        } else {
            Err(client_error(Status::NotFound)
                .with_explanation(format!("Object not found: {:?}", id)))
        }
    }

    /// Adds an object to the cloud. The object may already be present.
    pub fn add_object(&mut self, id: ObjectId, object: Object) -> Result<(), ClientError> {
        self.objects.insert(id, object);
        Ok(())
    }

    /// Atomically adds a series a commits to the cloud and updates the commit log. Commits that
    /// were already present are not re-added to the log.
    pub fn add_commits(&mut self, commits: Vec<(Commit, Option<Diff>)>) -> Result<(), ClientError> {
        let mut will_insert: Vec<(Commit, Option<Diff>)> = Vec::new();
        let mut new_commits: HashSet<CommitId> = HashSet::new();

        for (commit, diff) in commits {
            if let Some(_existing) = self.commits.get(&commit.id) {
                continue;
            }

            // Check that the same commit is not pushed twice in one pack.
            if new_commits.contains(&commit.id) {
                return Err(client_error(Status::ArgumentError)
                    .with_explanation(format!("commit {:?} pushed twice in a pack", commit.id)));
            }

            // Check that the base of the diff is known. This ensures that we don't create cycles in
            // the diff tree.
            if let Some(diff) = &diff {
                // If the base is a commit, we must have received this commit, either in this pack
                // or a previous AddCommits.
                if let PageState::AtCommit(base_id) = &diff.base_state {
                    if !self.commits.contains_key(&base_id) && !new_commits.contains(base_id) {
                        return Err(client_error(Status::NotFound).with_explanation(format!(
                            "commit {:?} sent with a diff based on commit \
                             {:?} but this commit is unknown to the cloud",
                            commit.id, base_id
                        )));
                    }
                }
            }

            new_commits.insert(commit.id.clone());
            will_insert.push((commit, diff))
        }

        if will_insert.is_empty() {
            return Ok(());
        }

        // Mutate the data structure.
        for (commit, diff) in will_insert {
            self.commit_log.push(commit.id.clone());
            if let Some(diff) = diff {
                self.add_diff(commit.id.clone(), diff);
            }
            self.commits.insert(commit.id.clone(), commit);
        }

        self.commit_signal.signal_and_rearm();
        Ok(())
    }

    /// Returns a future that will wake up on new commits, or None if position is not after the
    /// latest commit.
    pub fn watch(&self, position: Token) -> Option<PageCloudWatcher> {
        if position.0 < self.commit_log.len() {
            None
        } else {
            // We ignore cancellations, because extra watch notifications are OK.
            Some(self.commit_signal.watch())
        }
    }

    /// Returns a vector of new commits after position and a new position.
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

    /// Returns a diff from one of the diff bases, or from the origin of the commit.
    pub fn get_diff(&self, commit_id: CommitId, bases: Vec<CommitId>) -> Result<Diff, ClientError> {
        self.commits.get(&commit_id).ok_or(
            client_error(Status::NotFound)
                .with_explanation(format!("get_diff: commit {:?} not found", commit_id)),
        )?;
        let origin = self.get_origin(&commit_id);

        // Find the bases with the same origin. The origin is always a base.
        let mut possible_bases: Vec<PageState> = bases
            .into_iter()
            .filter(|b| self.get_origin(b) == origin)
            .map(PageState::AtCommit)
            .collect();
        possible_bases.push(origin);

        // Choose the shortest diff.
        Ok(possible_bases
            .into_iter()
            .map(|base| Diff {
                base_state: base.clone(),
                changes: self.compute_diff(base, PageState::AtCommit(commit_id.clone())),
            })
            .min_by_key(|d| d.changes.len())
            .unwrap())
    }

    /// Computes the diff from version `state1` to version `state2`. The versions must have the same
    /// diff origin.
    // When computing the diff between two states, we need to go up to their common diff ancestor in
    // the diff tree (if it exists). To make common ancestor computations easier, we precompute two
    // pieces of information:
    //  - An `origin`: this is the page state obtained by following diff bases until we reach a
    //    state that has no associated diff.
    //  - A `depth`: this is the number of diffs on the path from this state to the origin.
    //
    // Given two states A and B, if they have different origins, they have no common diff ancestor
    // in the diff tree. If they have the same origin, we can define the "ancestor at depth X of A"
    // as the (unique) commit of depth X that is on the path from A to its origin (this is easily
    // computed from the ancestor at depth X+1). Then, the closest common ancestor of A and B is
    // obtained by finding the highest X such that the ancestor at depth X of A is the ancestor at
    // depth X of B.
    fn compute_diff(&self, mut state1: PageState, mut state2: PageState) -> Vec<DiffEntry> {
        // Invariant: `diffX` is Some(..) if depthX > 0, None otherwise.
        let (mut depth1, mut diff1) = self.diff_one_step(&state1);
        let (mut depth2, mut diff2) = self.diff_one_step(&state2);

        // Stores the diffs found on the paths from state1 and state2.
        let mut state1_to_ancestor: Vec<&Diff> = Vec::new();
        let mut state2_to_ancestor: Vec<&Diff> = Vec::new();

        // Advance to the parent of the deepest of `state1` and `state2` until we end up at the same
        // commit. This terminates before both reach the origin.
        loop {
            if state1 == state2 {
                break;
            }

            assert!(depth1 > 0 || depth2 > 0, "Different origins for the two states");
            if depth1 <= depth2 {
                // We have `depth2 > 0` and we expect a parent diff.
                let diff = diff2.expect("Non-null depth but no diff");
                state2_to_ancestor.push(diff);
                state2 = diff.base_state.clone();
                let (new_depth, new_diff) = self.diff_one_step(&diff.base_state);
                depth2 = new_depth;
                diff2 = new_diff;
            } else {
                // We have `depth1 > 0` and we expect a parent diff.
                let diff = diff1.expect("Non-null depth but no diff");
                state1_to_ancestor.push(diff);
                state1 = diff.base_state.clone();
                let (new_depth, new_diff) = self.diff_one_step(&diff.base_state);
                depth1 = new_depth;
                diff1 = new_diff;
            }
        }

        // Configuration:
        //    (state1)                        (state2)
        //      ^                               ^
        //      | s1_to_ancestor[0]             | s2_to_ancestor[0]
        //    (...)                            (...)
        //      ^                               ^
        //      | s1_to_ancestor[n1]             | s2_to_ancestor[n2]
        //     (-------------- ancestor --------------)
        let mut accumulator = CompactingAccumulator::new();
        for diff in state1_to_ancestor.into_iter() {
            diff.changes.iter().for_each(|e| accumulator.push(e.clone().invert()));
        }
        for diff in state2_to_ancestor.into_iter().rev() {
            diff.changes.iter().for_each(|e| accumulator.push(e.clone()));
        }
        accumulator.done()
    }

    fn get_origin(&self, c: &CommitId) -> PageState {
        match self.diffs.get(c) {
            // TODO(ambre): remove when we don't need compatibility with non-diff sync.
            None => PageState::AtCommit(c.clone()),
            Some(diff) => diff.origin.clone(),
        }
    }

    /// If we have a diff for `state`, returns this diff and the (positive) depth of `state` in the
    /// diff tree. Otherwise, returns no diff and a depth of zero.
    fn diff_one_step(&self, state: &PageState) -> (usize, Option<&Diff>) {
        match state {
            PageState::EmptyPage => (0, None),
            PageState::AtCommit(c) => {
                match self.diffs.get(c) {
                    // TODO(ambre): remove when we don't need compatibility with non-diff sync.
                    None => (0, None),
                    Some(diff_meta) => (diff_meta.depth, Some(&diff_meta.diff)),
                }
            }
        }
    }

    /// Adds a diff and the appropriate metadata to the diff tree.  Assumes that the diff base
    /// exists and the commit has not been inserted before.
    fn add_diff(&mut self, commit_id: CommitId, diff: Diff) {
        let (origin, depth) = match &diff.base_state {
            PageState::EmptyPage => (PageState::EmptyPage, 1),
            PageState::AtCommit(base_commit_id) => {
                match self.diffs.get(base_commit_id) {
                    Some(diff) => (diff.origin.clone(), diff.depth + 1),
                    // TODO(ambre): remove when we remove compatibility with non-diff Ledgers.
                    None => (PageState::AtCommit(base_commit_id.clone()), 1),
                }
            }
        };
        self.diffs.insert(commit_id, DiffTreeEntry { origin, depth, diff });
    }
}

impl Cloud {
    /// Creates a new, empty cloud storage.
    pub fn new() -> Cloud {
        Self { device_set: DeviceSet::new(), pages: HashMap::new() }
    }

    /// Returns the page with the given id. The page is created if absent.
    pub fn get_page(&mut self, id: PageId) -> &mut PageCloud {
        self.pages.entry(id).or_insert_with(|| PageCloud::new())
    }

    /// Returns the device set.
    pub fn get_device_set(&mut self) -> &mut DeviceSet {
        &mut self.device_set
    }
}

pub type DeviceSetWatcher = SignalWatcher;
impl DeviceSet {
    /// Creates a new, empty device set.
    pub fn new() -> DeviceSet {
        DeviceSet { fingerprints: HashSet::new(), erasure_signal: Signal::new() }
    }

    /// Adds a fingerprint to the set. Nothing happens if the
    /// fingerprint is already present.
    pub fn set_fingerprint(&mut self, fingerprint: Fingerprint) {
        self.fingerprints.insert(fingerprint);
    }

    /// Checks that a fingerprint is present in the cloud.
    pub fn check_fingerprint(&self, fingerprint: &Fingerprint) -> bool {
        self.fingerprints.contains(fingerprint)
    }

    /// Erases all fingerprints and calls the watchers.
    pub fn erase(&mut self) {
        self.fingerprints.clear();
        self.erasure_signal.signal_and_rearm()
    }

    /// If `fingerprint` is present on the cloud, returns a future that
    /// completes when the cloud is erased. Otherwise, returns
    /// `None`.
    pub fn watch(&self, fingerprint: &Fingerprint) -> Option<DeviceSetWatcher> {
        if !self.fingerprints.contains(fingerprint) {
            return None;
        }
        Some(self.erasure_signal.watch())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn vec_u8(s: &str) -> Vec<u8> {
        s.as_bytes().to_owned()
    }

    fn commit_id(s: &str) -> CommitId {
        CommitId(vec_u8(s))
    }

    fn commit(id: &str) -> Commit {
        Commit { id: commit_id(id), data: format!("commit_{}_data", id).into_bytes() }
    }

    fn insert(id: &str) -> DiffEntry {
        DiffEntry {
            entry_id: vec_u8(id),
            data: format!("data_{}", id).into_bytes(),
            operation: Operation::Insertion,
        }
    }
    fn delete(id: &str) -> DiffEntry {
        DiffEntry {
            entry_id: vec_u8(id),
            data: format!("data_{}", id).into_bytes(),
            operation: Operation::Deletion,
        }
    }

    fn base_state(base: Option<&str>) -> PageState {
        match base {
            None => PageState::EmptyPage,
            Some(id) => PageState::AtCommit(commit_id(id)),
        }
    }

    fn diff_from(from: Option<&str>, changes: Vec<DiffEntry>) -> Option<Diff> {
        Some(Diff { base_state: base_state(from), changes })
    }

    // Used to compare two vectors as sets.
    fn as_set<T: std::hash::Hash + Eq>(vec: Vec<T>) -> HashSet<T> {
        vec.into_iter().collect()
    }

    #[test]
    fn compacting_accumulator() {
        let mut acc = CompactingAccumulator::new();
        // Creates a sequence of operations: we insert/delete an entry 1/2/3 times, alternating the
        // operation, and starting with an insertion/deletion.
        for operation in vec![cloud::Operation::Insertion, cloud::Operation::Deletion] {
            for count in 1..=3 {
                let mut entry = DiffEntry {
                    entry_id: format!("entry_{:?}_{}", operation, count).into(),
                    operation,
                    data: format!("data_{:?}_{}", operation, count).into(),
                };
                for _ in 0..count {
                    acc.push(entry.clone());
                    entry.operation = entry.operation.invert();
                }
            }
        }

        let entries = acc.done();

        // The entries for i=2 cancel out, for i=1 and i=3, one entry remain (with operation equal
        // to the first operation).
        let mut reference_entries = HashSet::new();
        for operation in vec![cloud::Operation::Insertion, cloud::Operation::Deletion] {
            for count in [1, 3].iter() {
                reference_entries.insert(DiffEntry {
                    entry_id: format!("entry_{:?}_{}", operation, count).into(),
                    operation,
                    data: format!("data_{:?}_{}", operation, count).into(),
                });
            }
        }

        assert_eq!(as_set(entries), reference_entries);
    }

    #[test]
    fn missing_parent_rejected() {
        let mut page = PageCloud::new();
        let commits = vec![(commit("commit1"), diff_from(Some("commit2"), vec![]))];
        assert_eq!(page.add_commits(commits).unwrap_err().status(), Status::NotFound);
    }

    #[test]
    fn diff_cycle_rejected() {
        let mut page = PageCloud::new();
        let commits = vec![
            (commit("commit1"), diff_from(Some("commit2"), vec![])),
            (commit("commit2"), diff_from(Some("commit1"), vec![])),
        ];
        assert_eq!(page.add_commits(commits).unwrap_err().status(), Status::NotFound);
    }

    #[test]
    fn shortest_diff() {
        let mut page = PageCloud::new();
        let commits = vec![
            (commit("commit1"), diff_from(None, vec![insert("entry0")])),
            (commit("commit2"), diff_from(Some("commit1"), vec![insert("entry1")])),
            (commit("commit3"), diff_from(Some("commit2"), vec![delete("entry1")])),
        ];
        page.add_commits(commits).unwrap();

        // There are two possible diffs:
        //  - from the empty page to commit1, with one entry.
        //  - from commit3 to commit1, with zero entries.
        // Check that we select the shortest diff.
        let diff = page.get_diff(commit_id("commit1"), vec![commit_id("commit3")]).unwrap();
        assert_eq!(diff.base_state, PageState::AtCommit(commit_id("commit3")));
        assert!(diff.changes.is_empty());
    }

    #[test]
    fn shortest_diff_from_empty() {
        let mut page = PageCloud::new();
        let commits = vec![
            (commit("commit1"), diff_from(None, vec![insert("entry0")])),
            (
                commit("commit2"),
                diff_from(Some("commit1"), vec![delete("entry0"), insert("entry1")]),
            ),
        ];

        page.add_commits(commits).unwrap();

        // There are two possible diffs:
        //  - from the empty page to commit1, with one entry.
        //  - from commit2 to commit1, with two entries.
        // Check that we select the shortest diff.
        let diff = page.get_diff(commit_id("commit1"), vec![commit_id("commit2")]).unwrap();
        assert_eq!(diff.base_state, PageState::EmptyPage);
        assert_eq!(diff.changes.len(), 1);
    }

    #[test]
    fn complex_diff() {
        //  The diff tree is the following:
        //     (origin)
        //        | (size = 4)
        //      (ancestor)
        //       /      \  (sizes = 2, one common deletion)
        //     (A)      (B)
        //      |        |  (sizes = 1)
        //     (C)       (D)
        // If we ask for (D) with (C) as a possible base, we should get the diff from (C) to (D).
        let mut page = PageCloud::new();
        let commits = vec![
            (commit("origin"), None),
            (
                commit("ancestor"),
                diff_from(
                    Some("origin"),
                    vec![insert("e0"), delete("e1"), insert("e2"), insert("e3")],
                ),
            ),
            (commit("A"), diff_from(Some("ancestor"), vec![delete("e0"), insert("f0")])),
            (commit("B"), diff_from(Some("ancestor"), vec![delete("e0"), insert("g0")])),
            (commit("C"), diff_from(Some("A"), vec![insert("f1")])),
            (commit("D"), diff_from(Some("B"), vec![insert("g1")])),
        ];

        page.add_commits(commits).unwrap();
        // We can get a diff from the origin.
        let diff = page.get_diff(commit_id("D"), vec![]).unwrap();
        assert_eq!(diff.base_state, PageState::AtCommit(commit_id("origin")));
        let expected_changes =
            vec![delete("e1"), insert("e2"), insert("e3"), insert("g0"), insert("g1")];
        assert_eq!(as_set(diff.changes), as_set(expected_changes));

        // If we have C, we can get a shorter diff.
        let diff = page.get_diff(commit_id("D"), vec![commit_id("C")]).unwrap();
        assert_eq!(diff.base_state, PageState::AtCommit(commit_id("C")));
        let expected_changes = vec![delete("f1"), delete("f0"), insert("g0"), insert("g1")];
        assert_eq!(as_set(diff.changes), as_set(expected_changes));
    }
}
