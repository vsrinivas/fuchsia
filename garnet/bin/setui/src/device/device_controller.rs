use {
    crate::registry::base::{Command, Context, Notifier, SettingHandler, State},
    crate::registry::device_storage::DeviceStorageFactory,
    crate::switchboard::base::{DeviceInfo, SettingResponse},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info,
    futures::StreamExt,
    parking_lot::RwLock,
    std::fs,
};

const BUILD_TAG_FILE_PATH: &str = "/config/build-info/version";

pub fn spawn_device_controller<T: DeviceStorageFactory + Send + Sync + 'static>(
    _context: &Context<T>,
) -> SettingHandler {
    let (device_handler_tx, mut device_handler_rx) = futures::channel::mpsc::unbounded::<Command>();

    let notifier_lock = RwLock::<Option<Notifier>>::new(None);

    fasync::spawn(async move {
        while let Some(command) = device_handler_rx.next().await {
            match command {
                Command::ChangeState(state) => match state {
                    State::Listen(notifier) => {
                        let mut n = notifier_lock.write();
                        *n = Some(notifier);
                    }
                    State::EndListen => {
                        let mut n = notifier_lock.write();
                        *n = None;
                    }
                },
                Command::HandleRequest(_request, responder) => {
                    // TODO (go/fxb/36506): Send error back to client through responder.
                    // Right now will panic in hanging_get_handler if Err is sent back.
                    let contents = fs::read_to_string(BUILD_TAG_FILE_PATH)
                        .expect("Could not read build tag file");
                    let device_info = DeviceInfo { build_tag: contents.trim().to_string() };
                    fx_log_info!("{:?}", device_info);
                    responder.send(Ok(Some(SettingResponse::Device(device_info)))).ok();
                }
            }
        }
    });
    device_handler_tx
}
