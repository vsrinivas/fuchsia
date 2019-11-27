use crate::switchboard::base::SettingRequestResponder;
use {
    crate::registry::base::{Command, Notifier, State},
    crate::registry::device_storage::{DeviceStorage, DeviceStorageCompatible},
    crate::switchboard::base::{DoNotDisturbInfo, SettingRequest, SettingResponse, SettingType},
    failure::{format_err, Error},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::lock::Mutex,
    futures::StreamExt,
    parking_lot::RwLock,
    std::sync::Arc,
};

const USER_DND_NAME: &str = "user_dnd";
const NIGHT_MODE_DND_NAME: &str = "night_mode_dnd";

impl DeviceStorageCompatible for DoNotDisturbInfo {
    const KEY: &'static str = "do_not_disturb_info";

    fn default_value() -> Self {
        DoNotDisturbInfo::new(false, false)
    }
}

// Controller that handles commands for SettingType::DoNotDisturb
pub fn spawn_do_not_disturb_controller(
    storage: Arc<Mutex<DeviceStorage<DoNotDisturbInfo>>>,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (do_not_disturb_handler_tx, mut do_not_disturb_handler_rx) =
        futures::channel::mpsc::unbounded::<Command>();

    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));

    fasync::spawn(async move {
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
                        SettingRequest::SetUserInitiatedDoNotDisturb(user_dnd) => {
                            let mut request_info = stored_value.clone();
                            request_info.user_dnd = user_dnd;

                            write_value(
                                request_info,
                                USER_DND_NAME,
                                &notifier_lock,
                                &storage,
                                responder,
                            )
                            .await;
                            stored_value = request_info;
                        }
                        SettingRequest::SetNightModeInitiatedDoNotDisturb(night_mode_dnd) => {
                            let mut request_info = stored_value.clone();
                            request_info.night_mode_dnd = night_mode_dnd;

                            write_value(
                                request_info,
                                NIGHT_MODE_DND_NAME,
                                &notifier_lock,
                                &storage,
                                responder,
                            )
                            .await;
                            stored_value = request_info;
                        }
                        SettingRequest::Get => {
                            let mut storage_lock = storage.lock().await;
                            let _ = responder.send(Ok(Some(SettingResponse::DoNotDisturb(
                                storage_lock.get().await,
                            ))));
                        }
                        _ => panic!("Unexpected command to do not disturb"),
                    }
                }
            }
        }
    });
    do_not_disturb_handler_tx
}

async fn write_value(
    request_info: DoNotDisturbInfo,
    setting_name: &str,
    notifier_lock: &Arc<RwLock<Option<Notifier>>>,
    storage: &Arc<Mutex<DeviceStorage<DoNotDisturbInfo>>>,
    responder: SettingRequestResponder,
) {
    let mut storage_lock = storage.lock().await;
    let write_result = storage_lock.write(&request_info, false).await;
    if write_result.is_err() {
        responder
            .send(Err(format_err!("Failed to write {} to persistent storage", setting_name)))
            .ok();
        return;
    }
    let _ = responder.send(Ok(None));
    notify(notifier_lock.clone()).unwrap_or_else(|e: failure::Error| {
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
