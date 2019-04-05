// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error, failure::ResultExt, fidl::endpoints::RequestStream, fidl_fuchsia_setui::*,
    fuchsia_async as fasync, futures::prelude::*,
};

pub async fn start_setui_service(channel: fasync::Channel) -> Result<(), Error> {
    let mut stream = SetUiServiceRequestStream::from_channel(channel);

    while let Some(event) = await!(stream.try_next()).context("error reading value from stream")? {
        await!(handler(event))?;
    }
    // event_listener will now be dropped, closing the listener
    Ok(())
}

async fn handler(event: SetUiServiceRequest) -> fidl::Result<()> {
    match event {
        SetUiServiceRequest::Mutate { setting_type, mutation, responder } => {
            responder.send(&mut mutate(setting_type, mutation))?;
        }
        _ => {}
    }

    Ok(())
}

fn mutate(_setting_type: SettingType, _mutation: Mutation) -> MutationResponse {
    MutationResponse { return_code: ReturnCode::Ok }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ok() {
        let string_mutation =
            StringMutation { operation: StringOperation::Update, value: "Hi".to_string() };

        let result = mutate(
            SettingType::Unknown,
            fidl_fuchsia_setui::Mutation::StringMutationValue(string_mutation),
        );

        assert_eq!(result, MutationResponse { return_code: ReturnCode::Ok });
    }

}
