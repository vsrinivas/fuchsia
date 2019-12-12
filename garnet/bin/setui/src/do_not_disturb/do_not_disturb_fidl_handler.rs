use {
    crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender},
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_settings::{
        DoNotDisturbMarker, DoNotDisturbRequest, DoNotDisturbRequestStream, DoNotDisturbSettings,
        DoNotDisturbWatchResponder, Error,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::lock::Mutex,
    futures::TryStreamExt,
    parking_lot::RwLock,
    std::sync::Arc,
};

impl Sender<DoNotDisturbSettings> for DoNotDisturbWatchResponder {
    fn send_response(self, data: DoNotDisturbSettings) {
        self.send(data).log_fidl_response_error(DoNotDisturbMarker::DEBUG_NAME);
    }
}

impl From<SettingResponse> for DoNotDisturbSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::DoNotDisturb(info) = response {
            let mut dnd_settings = fidl_fuchsia_settings::DoNotDisturbSettings::empty();
            dnd_settings.user_initiated_do_not_disturb = info.user_dnd;
            dnd_settings.night_mode_initiated_do_not_disturb = info.night_mode_dnd;
            dnd_settings
        } else {
            panic!("incorrect value sent to do_not_disturb");
        }
    }
}

fn to_request(settings: DoNotDisturbSettings) -> Option<SettingRequest> {
    let mut dnd_info = DoNotDisturbInfo::empty();
    dnd_info.user_dnd = settings.user_initiated_do_not_disturb;
    dnd_info.night_mode_dnd = settings.night_mode_initiated_do_not_disturb;
    Some(SettingRequest::SetDnD(dnd_info))
}

pub fn spawn_do_not_disturb_fidl_handler(
    switchboard_handle: Arc<RwLock<SwitchboardImpl>>,
    mut stream: DoNotDisturbRequestStream,
) {
    let switchboard_lock = switchboard_handle.clone();

    type DNDHangingGetHandler =
        Arc<Mutex<HangingGetHandler<DoNotDisturbSettings, DoNotDisturbWatchResponder>>>;
    let hanging_get_handler: DNDHangingGetHandler =
        HangingGetHandler::create(switchboard_handle, SettingType::DoNotDisturb);

    fasync::spawn(async move {
        while let Ok(Some(req)) = stream.try_next().await {
            // Support future expansion of FIDL
            #[allow(unreachable_patterns)]
            match req {
                DoNotDisturbRequest::Set { settings, responder } => {
                    if let Some(request) = to_request(settings) {
                        let (response_tx, response_rx) =
                            futures::channel::oneshot::channel::<SettingResponseResult>();
                        if switchboard_lock
                            .write()
                            .request(SettingType::DoNotDisturb, request, response_tx)
                            .is_ok()
                        {
                            fasync::spawn(async move {
                                match response_rx.await {
                                    Ok(_) => responder
                                        .send(&mut Ok(()))
                                        .log_fidl_response_error(DoNotDisturbMarker::DEBUG_NAME),
                                    Err(_) => responder
                                        .send(&mut Err(Error::Failed))
                                        .log_fidl_response_error(DoNotDisturbMarker::DEBUG_NAME),
                                };
                            });
                        } else {
                            responder
                                .send(&mut Err(Error::Failed))
                                .log_fidl_response_error(DoNotDisturbMarker::DEBUG_NAME);
                        }
                    } else {
                        responder
                            .send(&mut Err(Error::Failed))
                            .log_fidl_response_error(DoNotDisturbMarker::DEBUG_NAME);
                    }
                }
                DoNotDisturbRequest::Watch { responder } => {
                    let mut hanging_get_lock = hanging_get_handler.lock().await;
                    hanging_get_lock.watch(responder).await;
                }
                _ => {
                    fx_log_err!("Unsupported DoNotDisturbRequest type");
                }
            }
        }
    })
}
