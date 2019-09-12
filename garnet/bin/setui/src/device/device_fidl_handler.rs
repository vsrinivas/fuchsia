use {
    crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender},
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    fidl_fuchsia_settings::{
        DeviceRequest, DeviceRequestStream, DeviceSettings, DeviceWatchResponder,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::lock::Mutex,
    futures::TryStreamExt,
    std::sync::{Arc, RwLock},
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
    switchboard_handle: Arc<RwLock<SwitchboardImpl>>,
    mut stream: DeviceRequestStream,
) {
    type DeviceHangingGetHandler =
        Arc<Mutex<HangingGetHandler<DeviceSettings, DeviceWatchResponder>>>;
    let hanging_get_handler: DeviceHangingGetHandler =
        HangingGetHandler::create(switchboard_handle, SettingType::Device);

    fasync::spawn(async move {
        while let Ok(Some(req)) = stream.try_next().await {
            // Support future expansion of FIDL
            #[allow(unreachable_patterns)]
            match req {
                DeviceRequest::Watch { responder } => {
                    let mut hanging_get_lock = hanging_get_handler.lock().await;
                    hanging_get_lock.watch(responder).await;
                }
                _ => {
                    fx_log_err!("Unsupported DeviceRequest type");
                }
            }
        }
    });
}
