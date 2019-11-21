// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines an interface for request filters and some basic filters.

use fidl_fuchsia_ledger_cloud::{DeviceSetRequest, PageCloudRequest, PositionToken};
use std::cell::RefCell;
use std::collections::hash_map::{DefaultHasher, Entry, HashMap};
use std::hash::{Hash, Hasher};

use crate::types::*;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Status {
    Ok,
    NetworkError,
}

pub trait RequestFilter {
    /// Decides whether to disconnect the PageCloudWatcher.
    fn page_cloud_watcher_status(&self) -> Status;
    /// Decides whether to disconnect the DeviceSetWatcher.
    fn device_set_watcher_status(&self) -> Status;
    /// Decides what to do with a DeviceSetRequest.
    fn device_set_request_status(&self, req: &DeviceSetRequest) -> Status;
    /// Decides what to do with a PageCloudRequest.
    fn page_cloud_request_status(&self, req: &PageCloudRequest) -> Status;
}

/// Filter that always returns the same status for all requests.
pub struct Always(Status);

impl Always {
    pub fn new(status: Status) -> Always {
        Always(status)
    }
}

impl RequestFilter for Always {
    fn page_cloud_watcher_status(&self) -> Status {
        self.0
    }

    fn device_set_watcher_status(&self) -> Status {
        self.0
    }

    fn device_set_request_status(&self, _req: &DeviceSetRequest) -> Status {
        self.0
    }

    fn page_cloud_request_status(&self, _req: &PageCloudRequest) -> Status {
        self.0
    }
}

/// Flaky network simulator: a given request on the PageCloud is
/// accepted after `required_retry_count` retries of this request. The
/// counter of a request is reset after it succeeds.
/// The behavior matches the INJECT_NETWORK_ERROR mode of the C++ fake
/// cloud provider: requests on the device set do not fail, and
/// watchers are not disconnected.
pub struct Flaky {
    /// The number of errors to return for each request before a success.
    required_retry_count: u64,
    /// A map associating request signatures (obtained by hashing the
    /// relevant part of the request) to the number of errors that
    /// should still be returned for this request.
    remaining_retries: RefCell<HashMap<u64, u64>>,
}

impl Flaky {
    /// Creates a new `NetworkErrorInjector` injecting
    /// `required_retry_count` errors for each request.
    pub fn new(required_retry_count: u64) -> Flaky {
        Flaky { required_retry_count, remaining_retries: RefCell::new(HashMap::new()) }
    }

    /// Returns a signature for a request, or `None` for requests that
    /// should never fail.
    fn signature(req: &PageCloudRequest) -> Option<u64> {
        // We cannot Hash requests directly because they contain FIDL
        // responders.
        let mut hasher = DefaultHasher::new();
        std::mem::discriminant(req).hash(&mut hasher);
        match req {
            PageCloudRequest::AddCommits { commits, .. } => {
                if let Ok(commits) = Commit::deserialize_pack(&commits) {
                    for (commit, _diff) in commits.into_iter() {
                        commit.id.0.hash(&mut hasher);
                    }
                }
            }
            PageCloudRequest::GetCommits { min_position_token, .. } => {
                Self::hash_token(min_position_token, &mut hasher)
            }
            PageCloudRequest::AddObject { id, buffer: _, .. } => {
                // Don't hash the buffer, it may be encrypted
                // differently each time.
                id.hash(&mut hasher);
            }
            PageCloudRequest::GetObject { id, .. } => id.hash(&mut hasher),
            PageCloudRequest::GetDiff { commit_id, .. } => commit_id.hash(&mut hasher),
            PageCloudRequest::SetWatcher { .. } => return None,
            PageCloudRequest::UpdateClock { .. } => return None,
        }
        return Some(hasher.finish());
    }

    fn hash_token(token: &Option<Box<PositionToken>>, hasher: &mut DefaultHasher) {
        if let Some(token) = token {
            token.opaque_id.hash(hasher)
        }
    }
}

impl RequestFilter for Flaky {
    fn page_cloud_watcher_status(&self) -> Status {
        Status::Ok
    }

    fn device_set_watcher_status(&self) -> Status {
        Status::Ok
    }

    fn device_set_request_status(&self, _req: &DeviceSetRequest) -> Status {
        Status::Ok
    }

    fn page_cloud_request_status(&self, req: &PageCloudRequest) -> Status {
        let sig = match Self::signature(req) {
            None => return Status::Ok,
            Some(sig) => sig,
        };
        let mut map = self.remaining_retries.borrow_mut();
        match map.entry(sig) {
            Entry::Occupied(mut entry) => {
                if *entry.get() == 0 {
                    // It's worth removing the entries after requests
                    // are successful, as most requests will be retried
                    // until they succeed, and will not use more space
                    // in the map after succeeding.
                    entry.remove();
                    Status::Ok
                } else {
                    entry.insert(entry.get() - 1);
                    Status::NetworkError
                }
            }
            Entry::Vacant(entry) => {
                entry.insert(self.required_retry_count - 1);
                Status::NetworkError
            }
        }
    }
}
