#![feature(async_await, await_macro, futures_api)]
#![recursion_limit = "1024"]

use failure::Error;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_mediasession::*;
use fuchsia_app as app;
use fuchsia_async as fasync;
use fuchsia_zircon::AsHandleRef;
use futures::prelude::*;
use futures::{
    channel::mpsc::{channel, Receiver},
    executor, select,
};
use std::io::{self, BufRead};

const HELP_TEXT: &str =
    r"This is a small utility that prints out active session changes as they occur in
the Media Session service and can send basic commands to the active session.

Commands:
    play
    pause
    stop
";

#[allow(unused)]
enum Never {}

type Result<T> = std::result::Result<T, Error>;

const CHANNEL_BUFFER_SIZE: usize = 10;

fn stdin_lines() -> Result<Receiver<String>> {
    let (mut line_sender, line_receiver) = channel(CHANNEL_BUFFER_SIZE);
    std::thread::spawn(move || {
        executor::block_on(
            async move {
                for line in io::stdin().lock().lines() {
                    let line = String::from(line.expect("Stdin line.").trim());
                    if let Err(e) = await!(line_sender.send(line)) {
                        eprintln!("Media Session Service task died: {:?}", e);
                    }
                }
            },
        )
    });
    Ok(line_receiver)
}

async fn control_active_session(
    registry_proxy: RegistryProxy,
    mut control_receiver: Receiver<PlaybackState>,
) -> Result<Never> {
    let mut event_stream = registry_proxy.take_event_stream();
    let mut active_session = None;
    loop {
        select! {
            event = event_stream.select_next_some() => {
                match event? {
                    RegistryEvent::OnActiveSessionChanged {
                        active_session: new_active_session
                    } => {
                        if let Some(session_id) = new_active_session.session_id {
                            let (client_end, server_end) = create_endpoints()?;
                            let koid = session_id.as_handle_ref().get_koid()?;
                            registry_proxy.connect_to_session_by_id(session_id, server_end)?;
                            let session_proxy = client_end.into_proxy()?;
                            let mut session_events = session_proxy.take_event_stream();
                            fasync::spawn(async move {
                                while let Ok(Some(event)) = await!(session_events.try_next()) {
                                    println!("Active session event:");
                                    println!("{:#?}", event);
                                }
                            });
                            active_session = Some(session_proxy);
                            println!("Active session is now {:?}.", koid);
                        } else {
                            println!("There is no active session.");
                            active_session = None;
                        }
                        registry_proxy.notify_active_session_change_handled()?;
                    }
                    RegistryEvent::OnSessionsChanged { sessions_change } => {
                        println!("Sessions collection changed: {:?}", sessions_change);
                        registry_proxy.notify_sessions_change_handled()?;
                    }
                }
            }
            new_state = control_receiver.select_next_some() => {
                if let Some(ref proxy) = &active_session {
                    match new_state {
                        PlaybackState::Paused => proxy.pause()?,
                        PlaybackState::Playing => proxy.play()?,
                        PlaybackState::Stopped => proxy.pause()?,
                        _ => (),
                    }
                } else {
                    println!("There is no active session to control.");
                }
            }
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    let registry_proxy = app::client::connect_to_service::<RegistryMarker>()?;
    let line_receiver = stdin_lines()?;
    let mut line_feed = line_receiver
        .take_while(|line| future::ready(line.as_str() != "quit"))
        .filter_map(|line| {
            future::ready(match line.as_str() {
                "pause" => Some(PlaybackState::Paused),
                "play" => Some(PlaybackState::Playing),
                "stop" => Some(PlaybackState::Stopped),
                _ => {
                    println!("Unrecognized commmand.");
                    println!("{}", HELP_TEXT);
                    None
                }
            })
        });

    let (mut control_sender, control_receiver) = channel(CHANNEL_BUFFER_SIZE);

    fasync::spawn(
        control_active_session(registry_proxy, control_receiver)
            .map_err(|e| eprintln!("{:?}", e))
            .map(|_| ()),
    );

    println!("{}", HELP_TEXT);

    await!(control_sender.send_all(&mut line_feed).map_err(|e| eprintln!("{:?}", e)).map(|_| ()));

    Ok(())
}
