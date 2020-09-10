// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_bluetooth_bredr::{
        ProtocolDescriptor, SearchResultsProxy, ServiceClassProfileIdentifier,
    },
    log::info,
    slab::Slab,
    std::collections::{HashMap, HashSet},
};

use crate::types::{RegisteredServiceId, ServiceRecord};

#[derive(Debug)]
struct SearchInfo {
    service_uuid: ServiceClassProfileIdentifier,

    attr_ids: Vec<u16>,

    proxy: SearchResultsProxy,

    /// Used to validate that data associated with an advertised service has not
    /// already been sent over the SearchResultsProxy.
    ///
    /// There should be at most one unique RegisteredServiceId associated with _any_
    /// service advertisement. See `crate::types` for more documentation about the
    /// `RegisteredServiceId` guarantees.
    sent_services: HashSet<RegisteredServiceId>,
}

impl SearchInfo {
    pub fn new(
        service_uuid: ServiceClassProfileIdentifier,
        attr_ids: Vec<u16>,
        proxy: SearchResultsProxy,
    ) -> Self {
        Self { service_uuid, attr_ids, proxy, sent_services: HashSet::new() }
    }

    pub fn service_uuid(&self) -> ServiceClassProfileIdentifier {
        self.service_uuid
    }

    /// Updates the SearchResultsProxy with an advertised service found in the piconet.
    ///
    /// The advertised service is specified by the `record`. If the service has already
    /// been reported to the SearchResultsProxy, it will be ignored.
    ///
    /// Returns an Error if the record is malformed.
    /// Returns true if the service was successfully relayed, false if not.
    pub fn send_service_found(&mut self, record: ServiceRecord) -> Result<bool, Error> {
        // The ServiceRecord only needs to be reported to this search once.
        let service_id = record.unique_service_id()?;
        if self.sent_services.contains(&service_id) {
            return Ok(false);
        }

        // Convert the record into the FIDL ServiceFound response.
        let mut response = record.to_service_found_response()?;
        let mut protocol = response.protocol.as_mut().map(|v| v.iter_mut());
        let protocol = protocol
            .as_mut()
            .map(|v| -> &mut dyn ExactSizeIterator<Item = &mut ProtocolDescriptor> { v });

        let _ = self.proxy.service_found(
            &mut response.id.into(),
            protocol,
            &mut response.attributes.iter_mut(),
        );

        // This service has now been reported.
        self.sent_services.insert(service_id);
        Ok(true)
    }
}

/// Unique identifier for a service search by the `MockPeer`. This is used for internal
/// bookkeeping and has no meaning outside of this context.
type SearchHandle = usize;

/// The `SearchSet` stores the active searches for services for a peer.
/// It allows for the adding of multiple service searches, removal of any active
/// searches, and the ability to notify active searches with provided service
/// advertisements.
/// Typical usage is:
///   let searches = SearchSet::new();
///
///   search_id = ServiceClassProfileId::*;
///   handle = searches.add(search_id, ..);
///
///   service_record = ...;
///   notify_searches(search_id, service_record);
///
///   remove(handle);
pub struct SearchSet {
    search_to_handles: HashMap<ServiceClassProfileIdentifier, HashSet<SearchHandle>>,
    search_infos: Slab<SearchInfo>,
}

impl SearchSet {
    pub fn new() -> Self {
        Self { search_to_handles: HashMap::new(), search_infos: Slab::new() }
    }

    /// Returns the set of active searches, identified by their Service Class ID.
    pub fn get_active_searches(&self) -> HashSet<ServiceClassProfileIdentifier> {
        self.search_to_handles.keys().cloned().collect()
    }

    /// Notifies all the searches for service class `id` with the published
    /// `service`.
    ///
    /// Returns the number of searches that were notified.
    pub fn notify_searches(
        &mut self,
        id: &ServiceClassProfileIdentifier,
        service: ServiceRecord,
    ) -> usize {
        let mut notified = 0;
        if let Some(handles) = self.search_to_handles.get(id) {
            for handle in handles {
                let search = self.search_infos.get_mut(handle.clone()).unwrap();
                match search.send_service_found(service.clone()) {
                    Ok(true) => notified += 1,
                    Ok(false) => {}
                    Err(e) => info!("Error sending search results: {:?}", e),
                }
            }
        }
        notified
    }

    /// Adds a new search for `service_uuid` to the `SearchSet` database.
    ///
    /// Returns the SearchHandle assigned to the service search. The SearchHandle
    /// should be used to unregister the search when finished, using `self::remove_search`.
    pub fn add(
        &mut self,
        service_uuid: ServiceClassProfileIdentifier,
        attr_ids: Vec<u16>,
        proxy: SearchResultsProxy,
    ) -> SearchHandle {
        let info = SearchInfo::new(service_uuid, attr_ids, proxy);
        let next = self.search_infos.insert(info);
        self.search_to_handles.entry(service_uuid).or_insert(HashSet::new()).insert(next);
        next
    }

    /// Removes a search specified by the `handle`.
    pub fn remove(&mut self, handle: SearchHandle) -> bool {
        if self.search_infos.contains(handle) {
            let svc_id = self.search_infos.remove(handle).service_uuid();
            if let Some(mut handles) = self.search_to_handles.remove(&svc_id) {
                handles.remove(&handle);
                // If there are still handles associated with this `svc_id`, re-store them.
                if !handles.is_empty() {
                    self.search_to_handles.insert(svc_id, handles);
                }
            }
            return true;
        }
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::peer::service::tests::build_a2dp_service_record;
    use crate::types::Psm;

    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_bluetooth_bredr::{SearchResultsMarker, SearchResultsRequest};
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::PeerId;
    use futures::{pin_mut, stream::StreamExt, task::Poll};

    /// Tests the basic case of adding a search and removing the search.
    #[fasync::run_singlethreaded(test)]
    async fn test_add_and_remove_search() {
        let mut search_mgr = SearchSet::new();
        let mut expected_searches = HashSet::new();
        assert_eq!(expected_searches, search_mgr.get_active_searches());

        let search_id = ServiceClassProfileIdentifier::AudioSink;
        let attrs = vec![0, 1, 2];
        let (proxy, _server) = create_proxy::<SearchResultsMarker>().unwrap();
        let handle = search_mgr.add(search_id, attrs, proxy);
        expected_searches.insert(search_id);
        assert_eq!(expected_searches, search_mgr.get_active_searches());

        // Should be able to remove the search.
        assert!(search_mgr.remove(handle));
        // Removing a non-existent search is OK. Returns false.
        assert!(!search_mgr.remove(handle));
    }

    /// Tests adding and removing multiple searches.
    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_searches() {
        let mut search_mgr = SearchSet::new();
        let mut expected_searches = HashSet::new();

        let search_id1 = ServiceClassProfileIdentifier::AudioSink;
        let attrs1 = vec![0, 1, 2];
        let (proxy1, _server1) = create_proxy::<SearchResultsMarker>().unwrap();
        let handle1 = search_mgr.add(search_id1, attrs1, proxy1);
        expected_searches.insert(search_id1);
        assert_eq!(expected_searches, search_mgr.get_active_searches());

        // Adding another search for the same ServiceClassID is OK.
        let search_id2 = ServiceClassProfileIdentifier::AudioSink;
        let attrs2 = vec![0, 2];
        let (proxy2, _server2) = create_proxy::<SearchResultsMarker>().unwrap();
        let handle2 = search_mgr.add(search_id2, attrs2, proxy2);
        assert_eq!(expected_searches, search_mgr.get_active_searches());

        // Adding a differing search is OK.
        let search_id3 = ServiceClassProfileIdentifier::AvRemoteControl;
        let attrs3 = vec![];
        let (proxy3, _server3) = create_proxy::<SearchResultsMarker>().unwrap();
        let handle3 = search_mgr.add(search_id3, attrs3, proxy3);
        expected_searches.insert(search_id3);
        assert_eq!(expected_searches, search_mgr.get_active_searches());

        // Should be able to remove the search. There should still be one
        // Sink and one AVRCP search active.
        assert!(search_mgr.remove(handle2));
        assert_eq!(expected_searches, search_mgr.get_active_searches());

        // Removing other two searches is OK.
        assert!(search_mgr.remove(handle1));
        assert!(search_mgr.remove(handle3));
        assert_eq!(HashSet::new(), search_mgr.get_active_searches());
    }

    /// Tests the notifying of an outstanding search with service advertisements.
    /// Duplicate service advertisements should not be relayed.
    #[test]
    fn test_notify_search() {
        let mut exec = fasync::Executor::new().unwrap();

        let mut search_mgr = SearchSet::new();

        let search_id1 = ServiceClassProfileIdentifier::AudioSink;
        let attrs1 = vec![0, 1, 2];
        let (proxy1, mut server) = create_proxy_and_stream::<SearchResultsMarker>().unwrap();
        let _handle1 = search_mgr.add(search_id1, attrs1, proxy1);

        let server_fut = server.next();
        pin_mut!(server_fut);
        assert!(exec.run_until_stalled(&mut server_fut).is_pending());

        let mut fake_record = build_a2dp_service_record(Psm(25));
        let handle = RegisteredServiceId::new(PeerId(123), 99);
        fake_record.register_service_record(handle);

        // Notify the search of this service.
        search_mgr.notify_searches(&search_id1, fake_record.clone());
        match exec.run_until_stalled(&mut server_fut) {
            Poll::Ready(Some(Ok(SearchResultsRequest::ServiceFound { responder, .. }))) => {
                let _ = responder.send();
            }
            x => panic!("Expected ready but got: {:?}", x),
        }

        // Notify the search of the same service - shouldn't be notified.
        search_mgr.notify_searches(&search_id1, fake_record);
        assert!(exec.run_until_stalled(&mut server_fut).is_pending());
    }
}
