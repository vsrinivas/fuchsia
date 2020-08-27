// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fuchsia_component::server::ServiceObjLocal;
use futures::lock::Mutex;
use futures::prelude::*;
use std::pin::Pin;
use std::sync::Arc;
// Include Brightness Control FIDL bindings
use backlight::Backlight;
use control::{
    Control, ControlTrait, WatcherAdjustmentResponder, WatcherAutoResponder,
    WatcherCurrentResponder,
};
use fidl_fuchsia_ui_brightness::ControlRequestStream;
use fuchsia_async::{self as fasync};
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use futures::channel::mpsc::UnboundedReceiver;
use futures::future::{AbortHandle, Abortable};
use sender_channel::SenderChannel;
use sensor::Sensor;
use watch_handler::WatchHandler;

mod backlight;
mod control;
mod sender_channel;
mod sensor;

const ADJUSTMENT_DELTA: f32 = 0.1;

async fn run_brightness_server(
    mut stream: ControlRequestStream,
    control: Arc<Mutex<dyn ControlTrait>>,
) -> Result<(), Error> {
    fx_log_info!("New brightness server");
    let (initial_current, initial_auto) = get_initial_value(control.clone()).await?;

    let watch_auto_handler: Arc<Mutex<WatchHandler<bool, WatcherAutoResponder>>> =
        Arc::new(Mutex::new(WatchHandler::create(Some(initial_auto))));

    let (auto_channel_sender, auto_channel_receiver) = futures::channel::mpsc::unbounded::<bool>();

    let watch_current_handler: Arc<Mutex<WatchHandler<f32, WatcherCurrentResponder>>> =
        Arc::new(Mutex::new(WatchHandler::create(Some(initial_current))));
    let (current_channel_sender, current_channel_receiver) =
        futures::channel::mpsc::unbounded::<f32>();

    let watch_adjustment_handler: Arc<Mutex<WatchHandler<f32, WatcherAdjustmentResponder>>> =
        Arc::new(Mutex::new(WatchHandler::create_with_change_fn(
            Box::new(move |old_data: &f32, new_data: &f32| {
                (*new_data - *old_data).abs() >= ADJUSTMENT_DELTA
            }),
            Some(0.0),
        )));
    let (adjustment_channel_sender, adjustment_channel_receiver) =
        futures::channel::mpsc::unbounded::<f32>();

    let control_clone = control.clone();
    {
        let mut control = control_clone.lock().await;
        control.add_current_sender_channel(current_channel_sender).await;
        control.add_auto_sender_channel(auto_channel_sender).await;
        control.add_adjustment_sender_channel(adjustment_channel_sender).await;
    }

    let listen_current_task_abort_handle = start_listen_task(
        watch_current_handler.clone(),
        Arc::new(Mutex::new(current_channel_receiver)),
    );

    let listen_auto_task_abort_handle =
        start_listen_task(watch_auto_handler.clone(), Arc::new(Mutex::new(auto_channel_receiver)));

    let listen_adjustment_task_abort_handle = start_listen_task(
        watch_adjustment_handler.clone(),
        Arc::new(Mutex::new(adjustment_channel_receiver)),
    );

    while let Some(request) = stream.try_next().await.context("error running brightness server")? {
        let mut control = control.lock().await;
        control
            .handle_request(
                request,
                watch_current_handler.clone(),
                watch_auto_handler.clone(),
                watch_adjustment_handler.clone(),
            )
            .await;
    }
    listen_current_task_abort_handle.abort();
    listen_auto_task_abort_handle.abort();
    listen_adjustment_task_abort_handle.abort();
    Ok(())
}

fn start_listen_task<T: std::marker::Send, ST: std::marker::Send>(
    watch_handler: Arc<Mutex<WatchHandler<T, ST>>>,
    receiver: Arc<Mutex<UnboundedReceiver<T>>>,
) -> AbortHandle
where
    T: std::clone::Clone + 'static,
    ST: watch_handler::Sender<T> + 'static,
{
    let (abort_handle, abort_registration) = AbortHandle::new_pair();
    let receiver = receiver.clone();
    fasync::Task::spawn(
        Abortable::new(
            async move {
                while let Some(value) = receiver.lock().await.next().await {
                    let mut handler_lock = watch_handler.lock().await;
                    handler_lock.set_value(value);
                }
            },
            abort_registration,
        )
        .unwrap_or_else(|_| ()),
    )
    .detach();
    abort_handle
}

async fn get_initial_value(control: Arc<Mutex<dyn ControlTrait>>) -> Result<(f32, bool), Error> {
    let mut control = control.lock().await;
    let (backlight, auto_brightness_on) = control.get_backlight_and_auto_brightness_on();
    let backlight = backlight.lock().await;
    let initial_brightness = backlight.get_brightness().await.unwrap_or_else(|e| {
        fx_log_err!("Didn't get the initial brightness in watch due to err {}, assuming 1.0.", e);
        1.0
    });
    Ok((initial_brightness as f32, auto_brightness_on))
}

async fn run_brightness_service(
    fs: ServiceFs<ServiceObjLocal<'static, ControlRequestStream>>,
    control: Arc<Mutex<dyn ControlTrait>>,
    run_server: impl Fn(
        ControlRequestStream,
        Arc<Mutex<dyn ControlTrait>>,
    ) -> Pin<Box<dyn Future<Output = Result<(), Error>>>>,
) -> Result<(), Error> {
    const MAX_CONCURRENT: usize = 10_000;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |stream| {
        let control = control.clone();
        run_server(stream, control).unwrap_or_else(|e| fx_log_info!("{:?}", e))
    });
    fut.await;
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["brightness"])?;
    fx_log_info!("Started");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(|stream: ControlRequestStream| stream);
    fs.take_and_serve_directory_handle()?;

    let backlight = Backlight::new().await?;
    let backlight = Arc::new(Mutex::new(backlight));

    let sensor = Sensor::new().await;
    let sensor = Arc::new(Mutex::new(sensor));

    let current_sender_channel: SenderChannel<f32> = SenderChannel::new();
    let current_sender_channel = Arc::new(Mutex::new(current_sender_channel));

    let auto_sender_channel: SenderChannel<bool> = SenderChannel::new();
    let auto_sender_channel = Arc::new(Mutex::new(auto_sender_channel));

    let adjustment_sender_channel: SenderChannel<f32> = SenderChannel::new();
    let adjustment_sender_channel = Arc::new(Mutex::new(adjustment_sender_channel));

    let control = Control::new(
        sensor,
        backlight,
        current_sender_channel,
        auto_sender_channel,
        adjustment_sender_channel,
    )
    .await;
    let control = Arc::new(Mutex::new(control));

    let run_server: fn(
        stream: ControlRequestStream,
        control: Arc<Mutex<dyn ControlTrait>>,
    ) -> Pin<Box<dyn Future<Output = Result<(), Error>>>> =
        |stream, control| Box::pin(run_brightness_server(stream, control));

    // TODO(kpt) Writes FIDL client unit test for this.
    run_brightness_service(fs, control, run_server).await?;
    Ok(())
}

#[cfg(test)]

mod tests {
    use super::*;

    fn mock_sender_channel() -> SenderChannel<f64> {
        SenderChannel::new()
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_send_value_in_channel_without_remove_any_sender() {
        let (channel_sender1, mut channel_receiver1) = futures::channel::mpsc::unbounded::<f64>();
        let (channel_sender2, mut channel_receiver2) = futures::channel::mpsc::unbounded::<f64>();
        let mut mock_sender_channel = mock_sender_channel();
        mock_sender_channel.add_sender_channel(channel_sender1).await;
        mock_sender_channel.add_sender_channel(channel_sender2).await;
        mock_sender_channel.send_value(12.0);
        assert_eq!(Some(12.0), channel_receiver1.next().await);
        assert_eq!(Some(12.0), channel_receiver2.next().await);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_send_value_in_channel_with_remove_a_sender() {
        let (channel_sender1, mut channel_receiver1) = futures::channel::mpsc::unbounded::<f64>();
        let (channel_sender2, mut channel_receiver2) = futures::channel::mpsc::unbounded::<f64>();
        let mut mock_sender_channel = mock_sender_channel();
        mock_sender_channel.add_sender_channel(channel_sender1).await;
        mock_sender_channel.add_sender_channel(channel_sender2).await;
        mock_sender_channel.sender_channel_vec.write()[0].close_channel();
        mock_sender_channel.send_value(12.0);
        assert_eq!(None, channel_receiver1.next().await);
        assert_eq!(Some(12.0), channel_receiver2.next().await);
    }
}
