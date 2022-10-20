// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device_info::DeviceInfoImpl;
use crate::handlebars_utils::TemplateEngine;
use mockall::automock;
use std::fs;
use std::path::Path;

type ResponderRequest = hyper::Request<hyper::Body>;
type ResponderResult = Result<hyper::Response<hyper::Body>, hyper::http::Error>;

#[automock]
pub trait Responder: Send + Sync {
    fn handle(&self, request: ResponderRequest) -> ResponderResult;
}

pub struct ResponderImpl {
    pub template_engine: Box<dyn TemplateEngine>,
    pub device_info: Box<DeviceInfoImpl>,
}

impl ResponderImpl {
    pub fn new(template_engine: Box<dyn TemplateEngine>, device_info: Box<DeviceInfoImpl>) -> Self {
        ResponderImpl { template_engine, device_info }
    }

    // If available, returns static content associated with requested path.
    pub fn get_static_content(&self, request: &str) -> Option<ResponderResult> {
        let raw_pkg_path = "/pkg".to_owned() + request;
        let canonical_pkg_path = Path::new(&raw_pkg_path).canonicalize().ok()?;

        if canonical_pkg_path.starts_with("/pkg/static/") && canonical_pkg_path.is_file() {
            if let Ok(content) = fs::read_to_string(canonical_pkg_path) {
                return Some(hyper::Response::builder().status(200).body(content.into()));
            }
        }

        None
    }

    /// Renders provided template with given render data.
    pub fn custom_template_content(
        &self,
        template_name: &str,
        status: hyper::StatusCode,
        data: &DeviceInfoImpl,
    ) -> Result<hyper::Response<hyper::Body>, hyper::http::Error> {
        match self.template_engine.render(template_name, data) {
            Ok(output) => hyper::Response::builder()
                .status(status)
                .header("Content-Type", "text/html")
                .body(output.into()),
            Err(_) => hyper::Response::builder()
                .body(format!("Unable to render template {:?}", template_name).into()),
        }
    }

    /// Renders provided template using default render data.
    pub fn simple_template_content(
        &self,
        template_name: &str,
        status: hyper::StatusCode,
    ) -> ResponderResult {
        self.custom_template_content(template_name, status, self.device_info.as_ref())
    }

    /// Return a result for given query path.
    pub fn template_content(&self, path: &str) -> ResponderResult {
        match path {
            "/" => self.simple_template_content("index", hyper::StatusCode::OK),
            "/info" => self.simple_template_content("info", hyper::StatusCode::OK),
            _ => self.simple_template_content("404", hyper::StatusCode::NOT_FOUND),
        }
    }
}

impl Responder for ResponderImpl {
    /// Returns a Response for given request. Unrecognized requests receive a 404 response.
    fn handle(&self, request: ResponderRequest) -> ResponderResult {
        let path = request.uri().path();

        if let Some(static_content) = self.get_static_content(path) {
            static_content
        } else {
            self.template_content(path)
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::device_info::DeviceInfoImpl;
    use crate::handlebars_utils::MockTemplateEngine;
    use crate::responder::{Responder, ResponderImpl};
    use mockall::predicate::{always, eq};
    use std::path::Path;

    const TEMPLATE_NAME_FOR_404_RESPONSE: &str = "404";
    const TEMPLATE_NAME_FOR_INDEX_RESPONSE: &str = "index";
    const TEMPLATE_NAME_FOR_INFO_RESPONSE: &str = "info";

    const RENDERED_TEMPLATE_CONTENT: &str = "rendered template content";

    const SAMPLE_GARBAGE_URI: &str = "http://127.0.0.1/garbage";

    const SAMPLE_INDEX_URI: &str = "http://127.0.0.1/";
    const SAMPLE_INFO_URI: &str = "http://127.0.0.1/info";

    const LEGAL_STATIC_URI: &str = "http://127.0.0.1/static/style.css";
    const ILLEGAL_STATIC_URI: &str = "http://127.0.0.1/static/../inaccessible/secret.txt";

    /// Verifies request for random garbage generates a 404 response.
    #[test]
    fn garbage_request_generates_404_response() -> std::result::Result<(), anyhow::Error> {
        // Mock a TemplateEngine expecting to render a "404" page.
        let mut template_engine = MockTemplateEngine::new();
        template_engine
            .expect_render()
            .with(eq(TEMPLATE_NAME_FOR_404_RESPONSE), always())
            .times(1)
            .returning(|_, _| Ok(RENDERED_TEMPLATE_CONTENT.to_string()));

        // Instance our responder-under-test.
        let responder =
            ResponderImpl::new(Box::new(template_engine), Box::new(DeviceInfoImpl::new()));

        // Create a "garbage" test request for our responder-under-test to handle.
        let garbage_request = hyper::Request::builder()
            .uri(SAMPLE_GARBAGE_URI)
            .body("test request".to_string().into())
            .unwrap();

        // Send our test request through our responder-under-test.
        let response = responder.handle(garbage_request)?;

        // Assert response to garbage request is a 404.
        assert_eq!(response.status(), hyper::StatusCode::NOT_FOUND);

        Ok(())
    }

    /// Verifies request for index ("/") generates a 200 response.
    #[test]
    fn index_request_generates_200_response() -> std::result::Result<(), anyhow::Error> {
        // Mock a TemplateEngine expecting to render an "index" page.
        let mut template_engine = MockTemplateEngine::new();
        template_engine
            .expect_render()
            .with(eq(TEMPLATE_NAME_FOR_INDEX_RESPONSE), always())
            .times(1)
            .returning(|_, _| Ok(RENDERED_TEMPLATE_CONTENT.to_string()));

        // Instance our responder-under-test.
        let responder =
            ResponderImpl::new(Box::new(template_engine), Box::new(DeviceInfoImpl::new()));

        // Create a "index" test request for our responder-under-test to handle.
        let index_request = hyper::Request::builder()
            .uri(SAMPLE_INDEX_URI)
            .body("test request".to_string().into())
            .unwrap();

        // Send our test request through our responder-under-test.
        let response = responder.handle(index_request)?;

        // Assert we got a 200.
        assert_eq!(response.status(), hyper::StatusCode::OK);

        Ok(())
    }

    /// Verifies request for "/info" generates a 200 response.
    #[test]
    fn info_request_generates_200_response() -> std::result::Result<(), anyhow::Error> {
        // Mock a TemplateEngine expecting to render a "info" page.
        let mut template_engine = MockTemplateEngine::new();
        template_engine
            .expect_render()
            .with(eq(TEMPLATE_NAME_FOR_INFO_RESPONSE), always())
            .times(1)
            .returning(|_, _| Ok(RENDERED_TEMPLATE_CONTENT.to_string()));

        // Create responder-under-test with mocked handlebars and empty DeviceInfo.
        let responder =
            ResponderImpl::new(Box::new(template_engine), Box::new(DeviceInfoImpl::new()));

        // Create a "info" test request for our responder-under-test to handle.
        let info_request = hyper::Request::builder()
            .uri(SAMPLE_INFO_URI)
            .body("test request".to_string().into())
            .unwrap();

        // Send our test request through our responder-under-test.
        let response = responder.handle(info_request)?;

        // Assert we got a 200.
        assert_eq!(response.status(), hyper::StatusCode::OK);

        Ok(())
    }

    /// Verifies request for a "/static" test resource generates a 200 response.
    #[test]
    fn accessible_static_resource_found() -> std::result::Result<(), anyhow::Error> {
        let responder = ResponderImpl::new(
            Box::new(MockTemplateEngine::new()),
            Box::new(DeviceInfoImpl::new()),
        );

        let info_request = hyper::Request::builder()
            .uri(LEGAL_STATIC_URI)
            .body("test request".to_string().into())
            .unwrap();

        // Send our test request through our responder-under-test.
        let response = responder.handle(info_request)?;

        // Assert we got a 200 with no requests to the TemplateEngine.
        assert_eq!(response.status(), hyper::StatusCode::OK);

        Ok(())
    }

    /// Verifies request for an "inaccessible" test resource generates a 404 response.
    #[test]
    fn inaccessible_resource_not_found() -> std::result::Result<(), anyhow::Error> {
        // Assert the test file is present in build.
        assert!(Path::new("/pkg/inaccessible/secret.txt").is_file());

        // Mock a TemplateEngine expecting to render a "404" page.
        let mut template_engine = MockTemplateEngine::new();
        template_engine
            .expect_render()
            .with(eq(TEMPLATE_NAME_FOR_404_RESPONSE), always())
            .times(1)
            .returning(|_, _| Ok(RENDERED_TEMPLATE_CONTENT.to_string()));

        let responder =
            ResponderImpl::new(Box::new(template_engine), Box::new(DeviceInfoImpl::new()));

        let info_request = hyper::Request::builder()
            .uri(ILLEGAL_STATIC_URI)
            .body("test request".to_string().into())
            .unwrap();

        // Send our test request through our responder-under-test.
        let response = responder.handle(info_request)?;

        // Assert we got a 404 instead of the "inaccessible" resource.
        assert_eq!(response.status(), hyper::StatusCode::NOT_FOUND);

        Ok(())
    }
}
