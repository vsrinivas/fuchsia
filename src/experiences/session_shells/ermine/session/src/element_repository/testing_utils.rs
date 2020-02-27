// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::element_repository::{
        event_handler::EventHandler, ElementEvent, ElementManagerServer, ElementRepository,
    },
    async_trait::async_trait,
    element_management::{Element, ElementManager, ElementManagerError},
    fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_session::ElementControllerRequestStream,
    fidl_fuchsia_session::ElementSpec,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, lock::Mutex},
    std::{cell::RefCell, collections::HashMap, rc::Rc},
};

pub fn init_logger() {
    fuchsia_syslog::init_with_tags(&["workstation_session", "tests"])
        .expect("Failed to initialize logger.");
}

impl ElementRepository<TestElementManager> {
    pub fn new_for_test() -> Self {
        ElementRepository::new(Rc::new(TestElementManager::new()))
    }
}

impl ElementManagerServer<TestElementManager> {
    pub(crate) fn new_for_test() -> (Self, mpsc::UnboundedReceiver<ElementEvent>) {
        let (sender, receiver) = mpsc::unbounded();
        (ElementManagerServer::new(Rc::new(TestElementManager::new()), sender), receiver)
    }
}

pub struct TestElementManager(Mutex<RefCell<HashMap<String, zx::Channel>>>);

impl TestElementManager {
    pub fn new() -> TestElementManager {
        TestElementManager(Mutex::new(RefCell::new(HashMap::new())))
    }
}

#[async_trait]
impl ElementManager for TestElementManager {
    async fn launch_element(
        &self,
        spec: ElementSpec,
        child_name: &str,
        child_collection: &str,
    ) -> Result<Element, ElementManagerError> {
        let child_url = spec
            .component_url
            .ok_or_else(|| ElementManagerError::url_missing(child_name, child_collection))?;

        let (directory_channel, channel) = zx::Channel::create().map_err(|_| {
            ElementManagerError::not_created(
                child_name,
                child_collection,
                child_url.to_string(),
                fcomponent::Error::Internal,
            )
        })?;

        let channels = self.0.lock().await;
        channels.borrow_mut().insert(child_name.to_string(), channel);

        Ok(Element::from_directory_channel(
            directory_channel,
            child_name,
            &child_url,
            child_collection,
        ))
    }
}

pub fn make_mock_element() -> (Element, zx::Channel) {
    let (directory_channel, channel) =
        zx::Channel::create().expect("unable to create mock channel");
    (Element::from_directory_channel(directory_channel, "", "", ""), channel)
}

#[derive(Default)]
pub struct CallCountEventHandler {
    pub add_call_count: i32,
    pub shutdown_call_count: i32,
}

impl EventHandler for CallCountEventHandler {
    fn add_element(&mut self, _element: Element, _stream: Option<ElementControllerRequestStream>) {
        self.add_call_count += 1;
    }

    fn shutdown(&mut self) {
        self.shutdown_call_count += 1;
    }
}
