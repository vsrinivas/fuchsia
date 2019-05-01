// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::encoding::OutOfLine;
use fidl_fuchsia_ledger_cloud::{
    CloudProviderRequest, CloudProviderRequestStream, CommitPack, DeviceSetRequest,
    DeviceSetRequestStream, DeviceSetWatcherProxy, PageCloudRequest, PageCloudRequestStream,
    PageCloudWatcherProxy, Status,
};
use futures::future;
use futures::future::{FutureExt, LocalFutureObj};
use futures::prelude::*;
use futures::select;
use futures::stream::FuturesUnordered;
use std::cell::RefCell;
use std::convert::{Into, TryFrom};
use std::rc::Rc;

use crate::serialization::*;
use crate::state::*;
use crate::utils::FutureOrEmpty;

/// Shared data accessible by any connection derived from a CloudSession.
struct CloudSessionShared {
    storage: Rc<RefCell<Cloud>>,
}

/// The state of a DeviceSet connection.
struct DeviceSetSession {
    /// Shared state.
    shared: Rc<CloudSessionShared>,
    /// Stream of requests.
    requests: stream::Fuse<DeviceSetRequestStream>,
    /// If a watcher is set, contains the future from storage that
    /// completes when the cloud is erased, and the watcher to signal
    /// in that case.
    watcher: Option<(future::Fuse<DeviceSetWatcher>, DeviceSetWatcherProxy)>,
}

type DeviceSetSessionFuture = LocalFutureObj<'static, ()>;
impl DeviceSetSession {
    fn new(shared: Rc<CloudSessionShared>, requests: DeviceSetRequestStream) -> DeviceSetSession {
        DeviceSetSession { shared, requests: requests.fuse(), watcher: None }
    }

    fn handle_request(&mut self, req: DeviceSetRequest) -> Result<(), fidl::Error> {
        let mut storage = self.shared.storage.borrow_mut();
        let device_set = storage.get_device_set();
        match req {
            DeviceSetRequest::CheckFingerprint { fingerprint, responder } => responder.send(
                CloudError::status(device_set.check_fingerprint(&Fingerprint::from(fingerprint))),
            ),
            DeviceSetRequest::SetFingerprint { fingerprint, responder } => {
                device_set.set_fingerprint(Fingerprint::from(fingerprint));
                responder.send(Status::Ok)
            }
            DeviceSetRequest::Erase { responder } => {
                device_set.erase();
                responder.send(Status::Ok)
            }
            DeviceSetRequest::SetWatcher { fingerprint, watcher: watcher_channel, responder } => {
                let proxy = watcher_channel.into_proxy()?;
                match device_set.watch(&Fingerprint::from(fingerprint)) {
                    Err(e) => responder.send(Status::from(e)),
                    Ok(fut) => {
                        self.watcher.replace((fut.fuse(), proxy));
                        responder.send(Status::Ok)
                    }
                }
            }
        }
    }

    async fn handle_requests(mut self) -> Result<(), fidl::Error> {
        loop {
            select! {
                req = self.requests.try_next() =>
                    match req? {
                        None => return Ok(()),
                        Some(req) => self.handle_request(req)?
                    },
                _ = FutureOrEmpty(self.watcher.as_mut().map(|(w, _)| w)) => {
                    // self.watcher cannot be None here.
                    let (_, proxy) = self.watcher.take().unwrap();
                    proxy.on_cloud_erased()?
                }
            }
        }
    }

    /// Runs the device set.
    fn run(self) -> DeviceSetSessionFuture {
        LocalFutureObj::new(Box::new(self.handle_requests().map(|_| ())))
    }
}

/// A future corresponding to a connection to a PageWatcher.
type PageWatcherFuture = LocalFutureObj<'static, ()>;

/// The state of a PageCloud connection.
struct PageSession {
    /// Shared data.
    shared: Rc<CloudSessionShared>,
    /// Id of the page.
    page_id: PageId,
    /// The stream of requests on this connection.
    requests: stream::Fuse<PageCloudRequestStream>,
    /// If a watcher is set, the future that completes when the watcher disconnects.
    watcher: Option<future::Fuse<PageWatcherFuture>>,
}

/// The type of the future returned by `PageSession::run`.
type PageSessionFuture = LocalFutureObj<'static, ()>;

impl PageSession {
    fn new(
        shared: Rc<CloudSessionShared>,
        page_id: PageId,
        requests: PageCloudRequestStream,
    ) -> PageSession {
        PageSession { shared, page_id, requests: requests.fuse(), watcher: None }
    }

    /// State machine for the page watcher.
    /// A page watcher connection is at any point:
    ///  - waiting on new commits in storage
    ///  - waiting for the watcher to acknowledge previously sent commits.
    async fn run_page_watcher(
        shared: Rc<CloudSessionShared>,
        page_id: PageId,
        mut position: Token,
        proxy: PageCloudWatcherProxy,
    ) {
        loop {
            let fut = shared.storage.borrow_mut().get_page(page_id.clone()).watch(position);
            if let Some(fut) = fut {
                await!(fut).expect("Cloud state destoyed before PageSession");
            }
            let mut exclusive_storage = shared.storage.borrow_mut();
            if let Some((next_position, commits)) =
                exclusive_storage.get_page(page_id.clone()).get_commits(position)
            {
                position = next_position;
                let buf = Commit::serialize_vec(commits);
                // Release the storage before await-ing.
                std::mem::drop(exclusive_storage);

                match await!(
                    proxy.on_new_commits(&mut CommitPack { buffer: buf }, &mut position.into(),)
                ) {
                    Ok(()) => {}
                    Err(_) => return (), // Assume the connection closed.
                }
            }
        }
    }

    fn handle_request(&mut self, request: PageCloudRequest) -> Result<(), fidl::Error> {
        let mut storage = self.shared.storage.borrow_mut();
        let page = storage.get_page(self.page_id.clone());
        match request {
            PageCloudRequest::AddCommits { commits, responder } => {
                match Commit::deserialize_vec(commits.buffer) {
                    Err(e) => responder.send(Status::from(e)),
                    Ok(commits) => responder.send(CloudError::status(page.add_commits(commits))),
                }
            }
            PageCloudRequest::GetCommits { min_position_token, responder } => {
                match Token::try_from(min_position_token) {
                    Err(_) => responder.send(Status::ParseError, None, None),
                    Ok(position) => {
                        let (position, commits) = match page.get_commits(position) {
                            None => (None, Vec::new()),
                            Some((position, commits)) => (Some(position), commits),
                        };
                        let buf = Commit::serialize_vec(commits);
                        // This must live until the end of the call
                        let mut position = position.map(Token::into);
                        let position = position.as_mut().map(OutOfLine);
                        responder.send(
                            Status::Ok,
                            Some(OutOfLine(&mut CommitPack { buffer: buf })),
                            position,
                        )
                    }
                }
            }
            PageCloudRequest::AddObject { id, buffer, responder } => {
                let mut data = Vec::new();
                match read_buffer(buffer, &mut data) {
                    Err(_) => responder.send(Status::ArgumentError),
                    Ok(()) => responder.send(CloudError::status(
                        page.add_object(ObjectId::from(id), Object { data }),
                    )),
                }
            }
            PageCloudRequest::GetObject { id, responder } => {
                match page.get_object(&ObjectId(id.clone())) {
                    Err(e) => responder.send(Status::from(e), None),
                    Ok(obj) => responder.send(
                        Status::Ok,
                        Some(OutOfLine(
                            &mut write_buffer(obj.data.as_slice()).expect("Failed to write buffer"),
                        )),
                    ),
                }
            }
            PageCloudRequest::SetWatcher {
                min_position_token,
                watcher: watcher_channel,
                responder,
            } => match Token::try_from(min_position_token) {
                Err(_) => responder.send(Status::ParseError),
                Ok(position) => {
                    let proxy = watcher_channel.into_proxy()?;
                    let watcher = Self::run_page_watcher(
                        Rc::clone(&self.shared),
                        self.page_id.clone(),
                        position,
                        proxy,
                    );
                    let watcher = LocalFutureObj::new(Box::new(watcher)).fuse();
                    self.watcher.replace(watcher);
                    responder.send(Status::Ok)
                }
            },
        }
    }

    async fn handle_requests(mut self) -> Result<(), fidl::Error> {
        loop {
            select! {
                req = self.requests.try_next() => {
                    match req? {
                        None => return Ok(()),
                        Some(req) => self.handle_request(req)?,
                    }
                },
                () = FutureOrEmpty(self.watcher.as_mut()) => {
                    // The watcher has been disconnected.
                    self.watcher.take();
                }
            }
        }
    }

    fn run(self) -> PageSessionFuture {
        LocalFutureObj::new(Box::new(self.handle_requests().map(|_| ())))
    }
}

/// Holds the state of a PageCloud connection.
pub struct CloudSession {
    /// Shared CloudSession data.
    shared: Rc<CloudSessionShared>,
    /// The stream of incoming requests.
    requests: stream::Fuse<CloudProviderRequestStream>,
    /// Futures for each active DeviceSet connection.
    device_sets: FuturesUnordered<DeviceSetSessionFuture>,
    /// Futures for each active PageCloud connection.
    pages: FuturesUnordered<PageSessionFuture>,
}

type CloudSessionFuture = LocalFutureObj<'static, ()>;

impl CloudSession {
    pub fn new(storage: Rc<RefCell<Cloud>>, stream: CloudProviderRequestStream) -> CloudSession {
        CloudSession {
            shared: Rc::new(CloudSessionShared { storage }),
            requests: stream.fuse(),
            device_sets: FuturesUnordered::new(),
            pages: FuturesUnordered::new(),
        }
    }

    fn handle_request(&mut self, req: CloudProviderRequest) -> Result<(), fidl::Error> {
        match req {
            CloudProviderRequest::GetDeviceSet { device_set: device_set_channel, responder } => {
                let stream = device_set_channel.into_stream()?;
                self.device_sets.push(DeviceSetSession::new(Rc::clone(&self.shared), stream).run());
                responder.send(Status::Ok)
            }
            CloudProviderRequest::GetPageCloud { app_id, page_id, page_cloud, responder } => {
                let stream = page_cloud.into_stream()?;
                let page_id = PageId::from(app_id, page_id);
                self.pages.push(PageSession::new(Rc::clone(&self.shared), page_id, stream).run());
                responder.send(Status::Ok)
            }
        }
    }

    async fn handle_requests(mut self) -> Result<(), fidl::Error> {
        loop {
            select! {
                _ = self.device_sets.next() => {},
                _ = self.pages.next() => {},
                req = self.requests.try_next() =>
                    match req? {
                        Some(req) => self.handle_request(req)?,
                        None => return Ok(())
                    }
            }
        }
    }

    pub fn run(self) -> CloudSessionFuture {
        LocalFutureObj::new(Box::new(self.handle_requests().map(|_| ())))
    }
}
