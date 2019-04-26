// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl_fuchsia_setui::*;
use futures::prelude::*;

pub struct SetUIHandler {}

/// SetUIHandler handles all API calls for the service. It is intended to be
/// used as a single instance to service multiple streams.
impl SetUIHandler {
    /// In the future, will populate with supporting classes, such as adapters.
    pub fn new() -> SetUIHandler {
        Self {}
    }

    /// Asynchronous handling of the given stream. Note that we must consider the
    /// possibility of simultaneous active streams.
    pub async fn handle_stream(&self, mut stream: SetUiServiceRequestStream) -> Result<(), Error> {
        while let Some(req) = await!(stream.try_next())? {
            await!(self.handle_request(req))?;
        }

        Ok(())
    }

    /// Routes a given request to the proper handling function.
    pub async fn handle_request(&self, req: SetUiServiceRequest) -> Result<(), fidl::Error> {
        match req {
            SetUiServiceRequest::Mutate { setting_type, mutation, responder } => {
                let mut response = self.mutate(setting_type, mutation);
                responder.send(&mut response)?;
            }
            _ => {}
        }
        Ok(())
    }

    /// Applies a mutation
    fn mutate(&self, _setting_type: SettingType, _mutation: Mutation) -> MutationResponse {
        MutationResponse { return_code: ReturnCode::Ok }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A basic test to exercise that basic functionality works. In this case, we
    /// mutate the unknown type, reserved for testing. We should always immediately
    /// receive back an Ok response.
    #[test]
    fn test_ok() {
        let handler = SetUIHandler::new();
        let string_mutation =
            StringMutation { operation: StringOperation::Update, value: "Hi".to_string() };

        let result = handler.mutate(
            SettingType::Unknown,
            fidl_fuchsia_setui::Mutation::StringMutationValue(string_mutation),
        );

        assert_eq!(result, MutationResponse { return_code: ReturnCode::Ok });
    }

}
