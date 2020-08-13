// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Example player is a fake media player that publishes its media session with
//! fuchsia.media.sessions2.
//!
//! run `fx shell mediasession_cli_tool ls` and then start some of these players
//! with `fx shell run example_player` to see how it works.

#![recursion_limit = "256"]

use anyhow::{Context as _, Error};
use fidl::encoding::Decodable;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_media::{Metadata, Property, TimelineFunction, METADATA_LABEL_ARTIST};
use fidl_fuchsia_media_sessions2::*;
use fuchsia_async as fasync;
use fuchsia_component as component;
use fuchsia_zircon as zx;
use futures::prelude::*;

type Result<T> = std::result::Result<T, Error>;

struct Player {
    playing: bool,
}

impl Player {
    pub fn new() -> Self {
        Self { playing: false }
    }

    fn timeline_function(state: PlayerState) -> TimelineFunction {
        // Timeline functions are used to describe our playback rate and bounds.
        // See the documentation for `fuchsia.mediaplayer.TimelineFunction`.
        if state == PlayerState::Playing {
            TimelineFunction {
                subject_time: 0,
                reference_time: zx::Time::get(zx::ClockId::Monotonic).into_nanos(),
                subject_delta: 1,
                reference_delta: 1,
            }
        } else {
            TimelineFunction {
                subject_time: 0,
                reference_time: 0,
                subject_delta: 0,
                reference_delta: 0,
            }
        }
    }

    fn player_status(player_state: PlayerState) -> PlayerStatus {
        PlayerStatus {
            duration: Some(0),
            player_state: Some(player_state),
            timeline_function: Some(Self::timeline_function(player_state)),
            repeat_mode: Some(RepeatMode::Single),
            shuffle_on: Some(false),
            error: None,
            content_type: Some(ContentType::Other),
            ..Decodable::new_empty()
        }
    }

    fn change_player_state(&mut self, playing: bool) -> Option<PlayerInfoDelta> {
        if playing == self.playing {
            None
        } else {
            self.playing = playing;
            Some(PlayerInfoDelta {
                player_status: Some(if self.playing {
                    Self::player_status(PlayerState::Playing)
                } else {
                    Self::player_status(PlayerState::Paused)
                }),
                ..Decodable::new_empty()
            })
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    let (player_client_end, player_server_end) =
        create_endpoints::<PlayerMarker>().context("Creating session channels.")?;

    component::client::connect_to_service::<PublisherMarker>()
        .context("Connecting to publisher.")?
        .publish_player(
            player_client_end,
            PlayerRegistration {
                domain: Some("domain://example".to_string()),
                ..Decodable::new_empty()
            },
        )
        .context("Publishing our player client end.")?;
    println!("Registered with Fuchsia Media Session service");

    let mut player = Player::new();
    let mut requests = player_server_end.into_stream()?;

    let mut staged = Some(PlayerInfoDelta {
        local: Some(true),
        media_images: None,
        player_status: None,
        player_capabilities: Some(PlayerCapabilities {
            flags: Some(PlayerCapabilityFlags::Play | PlayerCapabilityFlags::Pause),
            ..Decodable::new_empty()
        }),
        metadata: Some(Metadata {
            properties: vec![Property {
                label: String::from(METADATA_LABEL_ARTIST),
                value: String::from("Sine"),
            }],
        }),
        ..Decodable::new_empty()
    });
    let mut hanging_get = None;

    while let Some(request) = requests.try_next().await? {
        match request {
            PlayerRequest::WatchInfoChange { responder } => {
                if let Some(_) = hanging_get.take() {
                    eprintln!("Service issued concurrent watches; this is illegal.");
                    return Ok(());
                }

                hanging_get = Some(responder);
            }
            PlayerRequest::Play { .. } => {
                println!("Received a play command.");
                staged = player.change_player_state(/*playing=*/ true);
            }
            PlayerRequest::Pause { .. } => {
                println!("Received a pause command");
                staged = player.change_player_state(/*playing=*/ false);
            }
            PlayerRequest::Stop { .. } => {
                println!("Received stop request; shutting down player.");
                return Ok(());
            }
            _ => {}
        }

        if staged.is_some() && hanging_get.is_some() {
            println!("Sending the session service an update.");
            hanging_get.take().unwrap().send(staged.take().unwrap())?;
        }
    }
    println!("Channel to session service closed.");

    Ok(())
}
