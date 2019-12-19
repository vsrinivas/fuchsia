use {
    crate::fidl_processor::process_stream,
    crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::Sender,
    fidl_fuchsia_settings::{
        DeviceMarker, DeviceRequest, DeviceRequestStream, DeviceSettings, DeviceWatchResponder,
    },
    fuchsia_syslog::fx_log_err,
    futures::future::LocalBoxFuture,
    futures::FutureExt,
};

impl Sender<DeviceSettings> for DeviceWatchResponder {
    fn send_response(self, data: DeviceSettings) {
        match self.send(data) {
            Ok(_) => {}
            Err(e) => fx_log_err!("failed to send device info, {:#?}", e),
        }
    }
}

impl From<SettingResponse> for DeviceSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Device(info) = response {
            let mut device_settings = fidl_fuchsia_settings::DeviceSettings::empty();
            device_settings.build_tag = Some(info.build_tag);
            device_settings
        } else {
            panic!("incorrect value sent to device handler");
        }
    }
}

pub fn spawn_device_fidl_handler(
    switchboard_handle: SwitchboardHandle,
    stream: DeviceRequestStream,
) {
    process_stream::<DeviceMarker, DeviceSettings, DeviceWatchResponder>(
        stream,
        switchboard_handle,
        SettingType::Device,
        Box::new(
            move |context,
                  req|
                  -> LocalBoxFuture<'_, Result<Option<DeviceRequest>, failure::Error>> {
                async move {
                    // Support future expansion of FIDL
                    #[allow(unreachable_patterns)]
                    match req {
                        DeviceRequest::Watch { responder } => {
                            context.watch(responder).await;
                        }
                        _ => {
                            return Ok(Some(req));
                        }
                    }

                    return Ok(None);
                }
                .boxed_local()
            },
        ),
    );
}
