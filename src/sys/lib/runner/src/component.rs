// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, async_trait::async_trait, fidl_fuchsia_sys2 as fsys, futures::prelude::*};

/// Object implementing this type can be killed by calling kill function.
#[async_trait]
pub trait Killable {
    /// Should kill self and do cleanup.
    /// Should not return error or panic, should log error instead.
    async fn kill(self);
}

/// Holds information about the component that allows the controller to
/// interact with and control the component.
pub struct Controller<C: Killable> {
    /// stream via which the component manager will ask the controller to
    /// manipulate the component
    request_stream: fsys::ComponentControllerRequestStream,

    /// killable object which kills underlying component.
    /// This would be None once the object is killed.
    killable: Option<C>,
}

impl<C: Killable> Controller<C> {
    /// Creates new instance
    pub fn new(killable: C, requests: fsys::ComponentControllerRequestStream) -> Controller<C> {
        Controller { killable: Some(killable), request_stream: requests }
    }

    /// Serve the request stream held by this Controller.
    pub async fn serve(mut self) -> Result<(), Error> {
        while let Ok(Some(request)) = self.request_stream.try_next().await {
            match request {
                fsys::ComponentControllerRequest::Stop { control_handle: c } => {
                    // for now, treat a stop the same as a kill because this
                    // is not yet implementing proper behavior to first ask the
                    // remote process to exit
                    self.kill().await;
                    c.shutdown();
                    break;
                }
                fsys::ComponentControllerRequest::Kill { control_handle: c } => {
                    self.kill().await;
                    c.shutdown();
                    break;
                }
            }
        }

        Ok(())
    }

    /// Kill the job and shutdown control handle supplied to this function.
    async fn kill(&mut self) {
        if self.killable.is_some() {
            self.killable.take().unwrap().kill().await;
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, anyhow::Context, fidl::endpoints::create_endpoints, fuchsia_async as fasync};

    struct FakeComponent<K>
    where
        K: FnOnce(),
    {
        pub onkill: Option<K>,
    }

    #[async_trait]
    impl<K> Killable for FakeComponent<K>
    where
        K: FnOnce() + std::marker::Send,
    {
        async fn kill(mut self) {
            let func = self.onkill.take().unwrap();
            func();
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_kill_component() -> Result<(), Error> {
        let (sender, recv) = futures::channel::oneshot::channel::<()>();
        let fake_component = FakeComponent {
            onkill: Some(move || {
                sender.send(()).unwrap();
            }),
        };

        let (client_endpoint, server_endpoint) =
            create_endpoints::<fsys::ComponentControllerMarker>()
                .expect("could not create component controller endpoints");

        let controller_stream =
            server_endpoint.into_stream().context("failed to convert server end to controller")?;

        let controller = Controller::new(fake_component, controller_stream);

        client_endpoint
            .into_proxy()
            .expect("conversion to proxy failed.")
            .kill()
            .expect("FIDL error returned from kill request to controller");

        // this should return after kill call
        controller.serve().await.expect("should not fail");

        // this means kill was called
        recv.await?;

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_stop_component() -> Result<(), Error> {
        let (sender, recv) = futures::channel::oneshot::channel::<()>();
        let fake_component = FakeComponent {
            onkill: Some(move || {
                sender.send(()).unwrap();
            }),
        };

        let (client_endpoint, server_endpoint) =
            create_endpoints::<fsys::ComponentControllerMarker>()
                .expect("could not create component controller endpoints");

        let controller_stream =
            server_endpoint.into_stream().context("failed to convert server end to controller")?;

        let controller = Controller::new(fake_component, controller_stream);

        client_endpoint
            .into_proxy()
            .expect("conversion to proxy failed.")
            .stop()
            .expect("FIDL error returned from kill request to controller");

        // this should return after stop call for now.
        controller.serve().await.expect("should not fail");

        // this means stop was called
        recv.await?;

        Ok(())
    }
}
