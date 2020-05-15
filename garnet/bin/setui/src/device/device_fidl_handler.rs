use {
    crate::fidl_hanging_get_responder,
    crate::fidl_processor::process_stream,
    crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::Sender,
    fidl_fuchsia_settings::{
        DeviceMarker, DeviceRequest, DeviceRequestStream, DeviceSettings, DeviceWatchResponder,
    },
    futures::future::LocalBoxFuture,
    futures::FutureExt,
};

fidl_hanging_get_responder!(DeviceSettings, DeviceWatchResponder, DeviceMarker::DEBUG_NAME);

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
    switchboard_client: SwitchboardClient,
    stream: DeviceRequestStream,
) {
    process_stream::<DeviceMarker, DeviceSettings, DeviceWatchResponder>(
        stream,
        switchboard_client,
        SettingType::Device,
        Box::new(
            move |context,
                  req|
                  -> LocalBoxFuture<'_, Result<Option<DeviceRequest>, anyhow::Error>> {
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
