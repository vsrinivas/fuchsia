// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl_fuchsia_test_audio_recording::{
    AudioRecordingControlRequest, AudioRecordingControlRequestStream, AudioRecordingError,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::stream::{StreamExt, TryStreamExt};
use std::convert::TryInto;
use std::sync::Arc;
use tracing::error;

mod audio_facade;

use crate::audio_facade::AudioFacade;

async fn handle_audio_request(
    facade: Arc<AudioFacade>,
    stream: AudioRecordingControlRequestStream,
) -> Result<(), fidl::Error> {
    stream
        .try_for_each(move |request| {
            let facade_clone = facade.clone();
            async move {
                match request {
                    // Handle each AudioRecordingControlRequest request by responding with the request
                    // value
                    AudioRecordingControlRequest::PutInputAudio {
                        index,
                        audio_data,
                        responder,
                    } => {
                        let result = facade_clone
                            .put_input_audio(audio_data, index.try_into().unwrap())
                            .await
                            .context("put_input_audio errored")
                            .unwrap();
                        responder.send(result).context("error sending response").unwrap();
                    }

                    AudioRecordingControlRequest::ClearInputAudio { index, responder } => {
                        let mut result =
                            match facade_clone.clear_input_audio(index.try_into().unwrap()).await {
                                Ok(_) => Ok(()),
                                Err(_) => Err(AudioRecordingError::Fail),
                            };
                        responder.send(&mut result).context("error sending response").unwrap();
                    }

                    AudioRecordingControlRequest::GetOutputAudio { responder } => {
                        let result = facade_clone
                            .get_output_audio()
                            .await
                            .context("get_output_audio errored")
                            .unwrap();
                        responder.send(result).context("error sending response").unwrap();
                    }

                    AudioRecordingControlRequest::StartInputInjection { index, responder } => {
                        let mut result = match facade_clone
                            .start_input_injection(index.try_into().unwrap())
                            .await
                        {
                            Ok(_) => Ok(()),
                            Err(_) => Err(AudioRecordingError::Fail),
                        };
                        responder.send(&mut result).context("error sending response").unwrap();
                    }

                    AudioRecordingControlRequest::StopInputInjection { responder } => {
                        let mut result = match facade_clone.stop_input_injection().await {
                            Ok(_) => Ok(()),
                            Err(_) => Err(AudioRecordingError::Fail),
                        };
                        responder.send(&mut result).context("error sending response").unwrap();
                    }
                    AudioRecordingControlRequest::StartOutputSave { responder } => {
                        let mut result = match facade_clone.start_output_save().await {
                            Ok(_) => Ok(()),
                            Err(_) => Err(AudioRecordingError::Fail),
                        };
                        responder.send(&mut result).context("error sending response").unwrap();
                    }
                    AudioRecordingControlRequest::StopOutputSave { responder } => {
                        let mut result = match facade_clone.stop_output_save().await {
                            Ok(_) => Ok(()),
                            Err(_) => Err(AudioRecordingError::Fail),
                        };
                        responder.send(&mut result).context("error sending response").unwrap();
                    }
                }
                Ok(())
            }
        })
        .await
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    let audio_facade = Arc::new(AudioFacade::new()?);

    fs.dir("svc").add_fidl_service(move |stream| {
        let audio_facade_clone = audio_facade.clone();
        fasync::Task::spawn(async move {
            handle_audio_request(audio_facade_clone, stream).await.unwrap_or_else(|e| {
                error!("Error handling audio_recording_control channel: {:?}", e)
            })
        })
        .detach();
    });

    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;

    Ok(())
}
