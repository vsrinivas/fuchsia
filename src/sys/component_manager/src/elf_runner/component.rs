// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{error::ElfRunnerError, runtime_dir::RuntimeDirectory},
    async_trait::async_trait,
    fidl::endpoints::Proxy,
    fidl_fuchsia_process_lifecycle::LifecycleProxy,
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, AsHandleRef, Job, Process, Task},
    futures::future::{BoxFuture, FutureExt},
    log::{error, warn},
    runner::component::Controllable,
    std::sync::Arc,
};

/// Structure representing a running elf component.
pub struct ElfComponent {
    /// Namespace directory for this component, kept just as a reference to
    /// keep the namespace alive.
    _runtime_dir: RuntimeDirectory,

    /// Job in which the underlying process that represents the component is
    /// running.
    job: Arc<Job>,

    /// Process made for the program binary defined for this component.
    process: Option<Arc<Process>>,

    /// Client end of the channel given to an ElfComponent which says it
    /// implements the Lifecycle protocol. If the component does not implement
    /// the protocol, this will be None.
    lifecycle_channel: Option<LifecycleProxy>,

    /// We need to remember if we marked the main process as critical, because if we're asked to
    /// kill a component that has such a marking it'll bring down everything.
    main_process_critical: bool,

    /// Any tasks spawned to serve this component. For example, stdout and stderr
    /// listeners are Task objects that live for the duration of the component's
    /// lifetime.
    _tasks: Vec<fasync::Task<()>>,

    /// URL with which the component was launched.
    component_url: String,
}

impl ElfComponent {
    pub fn new(
        _runtime_dir: RuntimeDirectory,
        job: Job,
        process: Process,
        lifecycle_channel: Option<LifecycleProxy>,
        main_process_critical: bool,
        tasks: Vec<fasync::Task<()>>,
        component_url: String,
    ) -> Self {
        Self {
            _runtime_dir,
            job: Arc::new(job),
            process: Some(Arc::new(process)),
            lifecycle_channel,
            main_process_critical,
            _tasks: tasks,
            component_url,
        }
    }

    /// Return a pointer to the Process, returns None if the component has no
    /// Process.
    pub fn copy_process(&self) -> Option<Arc<Process>> {
        self.process.clone()
    }

    /// Return a handle to the Job containing the process for this component.
    ///
    /// The rights of the job will be set such that the resulting handle will be apppropriate to
    /// use for diagnostics-only purposes. Right now that is ZX_RIGHTS_BASIC (which includes
    /// INSPECT).
    pub fn copy_job_for_diagnostics(&self) -> Result<Job, ElfRunnerError> {
        self.job.as_handle_ref().duplicate(zx::Rights::BASIC).map(|h| Job::from(h)).map_err(
            |status| {
                ElfRunnerError::component_job_duplication_error(self.component_url.clone(), status)
            },
        )
    }
}

#[async_trait]
impl Controllable for ElfComponent {
    async fn kill(mut self) {
        if self.main_process_critical {
            warn!("killing a component with 'main_process_critical', so this will also kill component_manager and all of its components");
        }
        let _ = self.job.kill().map_err(|e| error!("failed killing job during kill: {}", e));
    }

    fn stop<'a>(&mut self) -> BoxFuture<'a, ()> {
        if let Some(lifecycle_chan) = self.lifecycle_channel.take() {
            let _ = lifecycle_chan.stop();

            let job = self.job.clone();

            // If the component's main process is critical we must watch for
            // the main process to exit, otherwise we could end up killing that
            // process and therefore killing the root job.
            if self.main_process_critical {
                if self.process.is_none() {
                    // This is a bit strange because there's no process, but there is a lifecycle
                    // channel. Since there is no process it seems like killing it can't kill
                    // component manager.
                    warn!("killing job of component with 'main_process_critical' set because component has lifecycle channel, but no process main process.");
                    let _ = self.job.kill().map_err(|e| {
                        error!("failed killing job for component with no lifecycle channel: {}", e)
                    });
                    return async {}.boxed();
                }
                // Try to duplicate the Process handle so we can us it to wait for
                // process termination
                let proc_handle = self.process.take().unwrap();

                async move {
                    let _ = fasync::OnSignals::new(
                        &proc_handle.as_handle_ref(),
                        zx::Signals::PROCESS_TERMINATED,
                    )
                    .await
                    .map_err(|e| {
                        error!(
                        "killing component's job after failure waiting on process exit, err: {}",
                        e
                    )
                    });
                    let _ = job.kill().map_err(|e| {
                        error!("failed killing job in stop after lifecycle channel closed: {}", e)
                    });
                }
                .boxed()
            } else {
                async move {
                    let _ = lifecycle_chan.on_closed()
                    .await
                    .map_err(|e| {
                        error!(
                        "killing component's job after failure waiting on lifecycle channel, err: {}",
                        e
                        )
                    });
                    let _ = job.kill().map_err(|e| {
                        error!("failed killing job in stop after lifecycle channel closed: {}", e)
                    });
                }
                .boxed()
            }
        } else {
            if self.main_process_critical {
                warn!(
                    "killing job of component {} marked with 'main_process_critical' because \
                component does not implement Lifecycle, so this will also kill component_manager \
                and all of its components",
                    self.component_url
                );
            }
            let _ = self.job.kill().map_err(|e| {
                error!("failed killing job for component with no lifecycle channel: {}", e)
            });
            async {}.boxed()
        }
    }
}

impl Drop for ElfComponent {
    fn drop(&mut self) {
        // just in case we haven't killed the job already
        let _ = self.job.kill().map_err(|e| error!("failed to kill job in drop: {}", e));
    }
}
