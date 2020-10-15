// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_ui_scenic::{ScenicMarker, ScenicProxy},
    fuchsia_async::{self as fasync},
    fuchsia_component::client::connect_to_service,
    fuchsia_scenic, fuchsia_syslog as syslog,
    fuchsia_zircon::{Duration, Time},
    futures::{StreamExt, TryFutureExt},
};

use crate::view;

/// An `App` represents an instance of the Graphical session.
///
/// To start the session, create a new `App` instance and then
/// call `app.run().await`.
pub struct App {
    /// The Scenic session associated with this `App`.
    /// Note: This is not a Session in the Session Framework sense.
    session: fuchsia_scenic::SessionPtr,

    /// The `view::Context` which the `App` is responsible for updating the `presentation_time`
    /// of.
    context: view::ContextPtr,

    /// The `View` which contains the `App`'s content.
    view: view::View,
}

// The frequency at which an `App`'s `view` is updated.
const UPDATE_FREQUENCY_MILLIS: i64 = 10;

impl App {
    /// Creates a new `App` instance.
    ///
    /// # Returns
    /// A new `App` with an active Scenic session, or an error if a `View` cannot be created due to
    /// eiher the Scenic calls failing, or the `View` not being successfully created.
    pub async fn new() -> Result<App, Error> {
        let scenic = connect_to_service::<ScenicMarker>()?;
        let session = App::make_session(&scenic)?;

        let display_info = scenic.get_display_info().await?;
        let context = view::Context::new_ptr(session.clone(), display_info);
        let view = view::View::new(context.clone())?;
        Ok(App { session, context, view })
    }

    /// Creates a new Scenic session.
    ///
    /// # Parameters
    /// - `scenic`: The `ScenicProxy` which is used to create the Scenic `SessionPtr`.
    ///
    /// # Returns
    /// A `SessionPtr` representing the created Scenic session, or an `Error` if the creation
    /// failed.
    fn make_session(scenic: &ScenicProxy) -> Result<fuchsia_scenic::SessionPtr, Error> {
        let (session_proxy, session_request) = create_proxy()?;
        scenic.create_session(session_request, None)?;

        Ok(fuchsia_scenic::Session::new(session_proxy))
    }

    /// Runs the application indefinitely.
    ///
    /// # Returns
    /// `Ok` if the application ran successfully, or an `Error` if execution halted unexpectedly.
    pub async fn run(&mut self) -> Result<(), Error> {
        let timer = fasync::Interval::new(Duration::from_millis(UPDATE_FREQUENCY_MILLIS));

        // The timer triggers once per `UPDATE_FREQUENCY`, at which point `self.update()` is called
        // to update the view.
        timer.map(move |_| self.update()).collect::<()>().await;

        Ok(())
    }

    /// Updates the `View` associated with this `App`. This method is expected to be called in a
    /// loop, as each `update` produces a single new "frame."
    fn update(&mut self) {
        self.context.lock().unwrap().presentation_time = Time::get_monotonic();

        self.view.update(self.context.clone());

        fasync::Task::local(
            self.session
                .lock()
                .present(self.context.lock().unwrap().presentation_time.into_nanos() as u64)
                .map_ok(|_| ())
                .unwrap_or_else(|error| syslog::fx_log_err!("Present error: {:?}", error)),
        )
        .detach();
    }
}
