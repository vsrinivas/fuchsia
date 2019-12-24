use {
    crate::fidl_processor::process_stream,
    crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::Sender,
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_settings::{
        DoNotDisturbMarker, DoNotDisturbRequest, DoNotDisturbRequestStream, DoNotDisturbSettings,
        DoNotDisturbWatchResponder, Error,
    },
    fuchsia_async as fasync,
    futures::future::LocalBoxFuture,
    futures::FutureExt,
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
    switchboard_handle: SwitchboardHandle,
    stream: DoNotDisturbRequestStream,
) {
    process_stream::<DoNotDisturbMarker, DoNotDisturbSettings, DoNotDisturbWatchResponder>(stream, switchboard_handle, SettingType::DoNotDisturb, Box::new(
                move |context, req| -> LocalBoxFuture<'_, Result<Option<DoNotDisturbRequest>, anyhow::Error>> {
                    async move {
                        // Support future expansion of FIDL
                        #[allow(unreachable_patterns)]
                        match req {
                            DoNotDisturbRequest::Set { settings, responder } => {
                                if let Some(request) = to_request(settings) {
                                    let (response_tx, response_rx) =
                                        futures::channel::oneshot::channel::<SettingResponseResult>(
                                        );
                                    if context
                                        .switchboard
                                        .lock()
                                        .await
                                        .request(SettingType::DoNotDisturb, request, response_tx)
                                        .is_ok()
                                    {
                                        fasync::spawn(async move {
                                            match response_rx.await {
                                                Ok(_) => responder
                                                    .send(&mut Ok(()))
                                                    .log_fidl_response_error(
                                                        DoNotDisturbMarker::DEBUG_NAME,
                                                    ),
                                                Err(_) => responder
                                                    .send(&mut Err(Error::Failed))
                                                    .log_fidl_response_error(
                                                        DoNotDisturbMarker::DEBUG_NAME,
                                                    ),
                                            };
                                        });
                                    } else {
                                        responder
                                            .send(&mut Err(Error::Failed))
                                            .log_fidl_response_error(
                                                DoNotDisturbMarker::DEBUG_NAME,
                                            );
                                    }
                                } else {
                                    responder
                                        .send(&mut Err(Error::Failed))
                                        .log_fidl_response_error(DoNotDisturbMarker::DEBUG_NAME);
                                }
                            }
                            DoNotDisturbRequest::Watch { responder } => {
                                context.watch(responder).await
                            }
                            _ => {
                                return Ok(Some(req));
                            }
                        }

                        return Ok(None);
                    }
                    .boxed_local()
                },
            ));
}
