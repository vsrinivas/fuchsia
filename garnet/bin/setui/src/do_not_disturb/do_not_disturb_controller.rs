use crate::switchboard::base::SettingRequestResponder;
use {
    crate::registry::base::{Command, Context, Notifier, SettingHandler, State},
    crate::registry::device_storage::{
        DeviceStorage, DeviceStorageCompatible, DeviceStorageFactory,
    },
    crate::switchboard::base::{
        DoNotDisturbInfo, SettingRequest, SettingResponse, SettingType, SwitchboardError,
    },
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::lock::Mutex,
    futures::StreamExt,
    parking_lot::RwLock,
    std::sync::Arc,
};

impl DeviceStorageCompatible for DoNotDisturbInfo {
    const KEY: &'static str = "do_not_disturb_info";

    fn default_value() -> Self {
        DoNotDisturbInfo::new(false, false)
    }
}

// Controller that handles commands for SettingType::DoNotDisturb
pub fn spawn_do_not_disturb_controller<T: DeviceStorageFactory + Send + Sync + 'static>(
    context: &Context<T>,
) -> SettingHandler {
    let storage_handle = context.storage_factory_handle.clone();
    let (do_not_disturb_handler_tx, mut do_not_disturb_handler_rx) =
        futures::channel::mpsc::unbounded::<Command>();

    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));

    fasync::spawn(async move {
        let storage = storage_handle.lock().await.get_store::<DoNotDisturbInfo>();
        // Load stored value
        let mut stored_value: DoNotDisturbInfo;
        {
            let mut storage_lock = storage.lock().await;
            stored_value = storage_lock.get().await;
        }

        while let Some(command) = do_not_disturb_handler_rx.next().await {
            match command {
                Command::ChangeState(state) => match state {
                    State::Listen(notifier) => {
                        *notifier_lock.write() = Some(notifier);
                    }
                    State::EndListen => {
                        *notifier_lock.write() = None;
                    }
                },
                Command::HandleRequest(request, responder) =>
                {
                    #[allow(unreachable_patterns)]
                    match request {
                        SettingRequest::SetDnD(dnd_info) => {
                            if dnd_info.user_dnd.is_some() {
                                stored_value.user_dnd = dnd_info.user_dnd;
                            }
                            if dnd_info.night_mode_dnd.is_some() {
                                stored_value.night_mode_dnd = dnd_info.night_mode_dnd;
                            }

                            write_value(stored_value, &notifier_lock, &storage, responder).await;
                        }
                        SettingRequest::Get => {
                            let mut storage_lock = storage.lock().await;
                            let _ = responder.send(Ok(Some(SettingResponse::DoNotDisturb(
                                storage_lock.get().await,
                            ))));
                        }
                        _ => {
                            responder
                                .send(Err(SwitchboardError::UnimplementedRequest {
                                    setting_type: SettingType::DoNotDisturb,
                                    request: request,
                                }))
                                .ok();
                        }
                    }
                }
            }
        }
    });
    do_not_disturb_handler_tx
}

async fn write_value(
    request_info: DoNotDisturbInfo,
    notifier_lock: &Arc<RwLock<Option<Notifier>>>,
    storage: &Arc<Mutex<DeviceStorage<DoNotDisturbInfo>>>,
    responder: SettingRequestResponder,
) {
    let mut storage_lock = storage.lock().await;
    let write_result = storage_lock.write(&request_info, false).await;
    if let Err(_) = write_result {
        responder
            .send(Err(SwitchboardError::StorageFailure { setting_type: SettingType::DoNotDisturb }))
            .ok();
        return;
    }
    let _ = responder.send(Ok(None));
    notify(notifier_lock.clone()).unwrap_or_else(|e: anyhow::Error| {
        fx_log_err!("Error notifying do not disturb changes: {:#?}", e);
    });
}

// TODO: watch for changes on current do_not_disturb and notify changes
// that way instead.
fn notify(notifier_lock: Arc<RwLock<Option<Notifier>>>) -> std::result::Result<(), Error> {
    if let Some(notifier) = (*notifier_lock.read()).clone() {
        notifier.unbounded_send(SettingType::DoNotDisturb)?;
    }
    Ok(())
}
