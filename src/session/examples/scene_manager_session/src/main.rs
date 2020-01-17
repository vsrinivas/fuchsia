use {
    anyhow::Error,
    fidl_fuchsia_ui_scenic::ScenicMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    scene_management::{self, SceneManager},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let scenic = connect_to_service::<ScenicMarker>()?;
    let mut scene_manager = scene_management::FlatSceneManager::new(scenic, None, None).await?;

    let x = scene_manager.display_metrics.width_in_pips() / 2.0;
    let y = scene_manager.display_metrics.height_in_pips() / 2.0;
    scene_manager.set_cursor_location(x, y);

    scene_manager.present();

    loop {}
}
