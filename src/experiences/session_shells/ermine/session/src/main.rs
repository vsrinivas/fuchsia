// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This declaration is required to support the `select!`.
#![recursion_limit = "256"]

use {
    anyhow::{anyhow, Context as _, Error},
    fidl::endpoints::{create_endpoints, ClientEnd, DiscoverableProtocolMarker, Proxy},
    fidl_fuchsia_element::{
        GraphicalPresenterMarker, GraphicalPresenterProxy, GraphicalPresenterRequest,
        GraphicalPresenterRequestStream, ManagerMarker as ElementManagerMarker,
        ManagerProxy as ElementManagerProxy, ManagerRequest as ElementManagerRequest,
        ManagerRequestStream as ElementManagerRequestStream,
    },
    fidl_fuchsia_identity_account::{AccountManagerMarker, AccountMetadata, AccountProxy},
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_session_scene::ManagerMarker as SceneManagerMarker,
    fidl_fuchsia_sys::LauncherMarker,
    fidl_fuchsia_ui_app::ViewProviderMarker,
    fidl_fuchsia_ui_views as ui_views,
    fidl_fuchsia_ui_views::ViewRefInstalledMarker,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{connect_to_protocol, launch_with_options, App, LaunchOptions},
        server::ServiceFs,
    },
    fuchsia_zircon as zx,
    futures::{try_join, StreamExt, TryStreamExt},
    log::{error, info, warn},
    std::fs,
    std::rc::Rc,
    std::sync::{Arc, Weak},
};

enum ExposedServices {
    ElementManager(ElementManagerRequestStream),
    GraphicalPresenter(GraphicalPresenterRequestStream),
}

/// The maximum number of open requests to this component.
///
/// Currently we have this value set low because the only service we are serving
/// is the ElementManager service and we don't expect many connections to it at
/// any given time.
const NUM_CONCURRENT_REQUESTS: usize = 5;

/// A hardcoded password to send on the AccountManager interface.
const EMPTY_PASSWORD: &str = "";

/// A hardcoded name to set on all accounts we create via AccountManager.
const ACCOUNT_NAME: &str = "created_by_session";

async fn launch_ermine() -> Result<(App, zx::Channel), Error> {
    let launcher = connect_to_protocol::<LauncherMarker>()?;

    let (client_chan, server_chan) = zx::Channel::create().unwrap();

    let mut launch_options = LaunchOptions::new();
    launch_options.set_additional_services(
        vec![ElementManagerMarker::PROTOCOL_NAME.to_string()],
        client_chan,
    );

    // Check if shell is overridden. Otherwise start ermine shell's oobe.
    let shell_url = match fs::read_to_string("/config/data/shell") {
        Ok(url) => url,
        Err(_) => "fuchsia-pkg://fuchsia.com/oobe#meta/oobe.cmx".to_string(),
    };

    let app = launch_with_options(&launcher, shell_url, None, launch_options)?;

    Ok((app, server_chan))
}

async fn expose_services(
    graphical_presenter: GraphicalPresenterProxy,
    element_manager: ElementManagerProxy,
    ermine_services_server_end: zx::Channel,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();

    // Add services for component outgoing directory.
    fs.dir("svc").add_fidl_service(ExposedServices::GraphicalPresenter);
    fs.take_and_serve_directory_handle()?;

    // Add services served to Ermine over `ermine_services_server_end`.
    fs.add_fidl_service_at(ElementManagerMarker::PROTOCOL_NAME, ExposedServices::ElementManager);
    fs.serve_connection(ermine_services_server_end).unwrap();

    let graphical_presenter = Rc::new(graphical_presenter);
    let element_manager = Rc::new(element_manager);

    fs.for_each_concurrent(NUM_CONCURRENT_REQUESTS, |service_request: ExposedServices| {
        // It's a bit unforunate to clone both of these for each service request, since each service
        // requires only one of the two.  However, as long as we have an "async move" block, we must
        // clone the refs before they are moved into it.
        let graphical_presenter = graphical_presenter.clone();
        let element_manager = element_manager.clone();

        async move {
            match service_request {
                ExposedServices::ElementManager(request_stream) => {
                    run_proxy_element_manager_service(element_manager, request_stream)
                        .await
                        .unwrap_or_else(|e| error!("Failure in element manager proxy: {}", e));
                }
                ExposedServices::GraphicalPresenter(request_stream) => {
                    run_proxy_graphical_presenter_service(graphical_presenter, request_stream)
                        .await
                        .unwrap_or_else(|e| error!("Failure in graphical presenter proxy: {}", e));
                }
            }
        }
    })
    .await;

    Ok(())
}

async fn run_proxy_element_manager_service(
    element_manager: Rc<ElementManagerProxy>,
    mut request_stream: ElementManagerRequestStream,
) -> Result<(), Error> {
    while let Some(request) =
        request_stream.try_next().await.context("Failed to obtain next request from stream")?
    {
        match request {
            ElementManagerRequest::ProposeElement { spec, controller, responder } => {
                // TODO(fxbug.dev/47079): handle error
                let mut result = element_manager
                    .propose_element(spec, controller)
                    .await
                    .context("Failed to forward proxied request")?;
                let _ = responder.send(&mut result);
            }
        }
    }
    Ok(())
}

async fn run_proxy_graphical_presenter_service(
    graphical_presenter: Rc<GraphicalPresenterProxy>,
    mut request_stream: GraphicalPresenterRequestStream,
) -> Result<(), Error> {
    while let Some(request) =
        request_stream.try_next().await.context("Failed to obtain next request from stream")?
    {
        match request {
            GraphicalPresenterRequest::PresentView {
                view_spec,
                annotation_controller,
                view_controller_request,
                responder,
            } => {
                // TODO(fxbug.dev/47079): handle error
                let mut result = graphical_presenter
                    .present_view(view_spec, annotation_controller, view_controller_request)
                    .await
                    .context("Failed to forward proxied request")?;
                let _ = responder.send(&mut result);
            }
        }
    }
    Ok(())
}

async fn set_view_focus(
    weak_focuser: Weak<fidl_fuchsia_session_scene::ManagerProxy>,
    mut view_ref: ui_views::ViewRef,
) -> Result<(), Error> {
    // [ViewRef]'s are one-shot use only. Duplicate it for use in request_focus below.
    let mut viewref_dup = fuchsia_scenic::duplicate_view_ref(&view_ref)?;

    // Wait for the view_ref to signal its ready to be focused.
    let view_ref_installed = connect_to_protocol::<ViewRefInstalledMarker>()
        .context("Could not connect to ViewRefInstalledMarker")?;
    let watch_result = view_ref_installed.watch(&mut view_ref).await;
    match watch_result {
        // Handle fidl::Errors.
        Err(e) => Err(anyhow::format_err!("Failed with err: {}", e)),
        // Handle ui_views::ViewRefInstalledError.
        Ok(Err(value)) => Err(anyhow::format_err!("Failed with err: {:?}", value)),
        Ok(_) => {
            // Now set focus on the view_ref.
            if let Some(focuser) = weak_focuser.upgrade() {
                let focus_result = focuser.request_focus(&mut viewref_dup).await?;
                match focus_result {
                    Ok(()) => Ok(()),
                    Err(e) => Err(anyhow::format_err!("Failed with err: {:?}", e)),
                }
            } else {
                Err(anyhow::format_err!("Failed to acquire Focuser"))
            }
        }
    }
}

/// Use the AccountManager API (with the supplied password) to either get the only existing account
/// or create a new account then acquire a data directory for that account.
async fn get_account_directory(password: &str) -> Result<DirectoryProxy, Error> {
    let account_manager = Arc::new(connect_to_protocol::<AccountManagerMarker>().unwrap());
    info!("Connected to AccountManager");

    let account_ids = account_manager.get_account_ids().await?;
    let maybe_account_id = match account_ids.len() {
        0 => None,
        1 => Some(account_ids[0]),
        count => {
            return Err(anyhow!("Multiple ({}) accounts found, cannot get data directory", count));
        }
    };

    let (account_client_end, account_server_end) = create_endpoints()?;
    let account_metadata =
        AccountMetadata { name: Some(ACCOUNT_NAME.to_string()), ..AccountMetadata::EMPTY };

    match maybe_account_id {
        None => {
            info!("Creating a new account through AccountManager");
            account_manager
                .deprecated_provision_new_account(password, account_metadata, account_server_end)
                .await?
                .map_err(|err| anyhow!("Error provisioning new account: {:?}", err))?;
        }
        Some(account_id) => {
            info!("Getting existing account with ID {}", account_id);
            account_manager
                .deprecated_get_account(account_id, password, account_server_end)
                .await?
                .map_err(|err| anyhow!("Error getting account: {:?}", err))?;
        }
    }

    let account: AccountProxy = account_client_end.into_proxy()?;
    let (directory_client_end, directory_server_end) = create_endpoints()?;
    info!("Getting directory on account");
    account
        .get_data_directory(directory_server_end)
        .await?
        .map_err(|err| anyhow!("Error getting data directory: {:?}", err))?;
    Ok(directory_client_end.into_proxy()?)
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["workstation_session"]).expect("Failed to initialize logger.");

    let (app, ermine_services_server_end) = launch_ermine().await?;
    let view_provider = app.connect_to_protocol::<ViewProviderMarker>()?;

    // Attempt to retrieve a data directory for the account from AccountManager. If this fails
    // just continue without the directory.
    match get_account_directory(EMPTY_PASSWORD).await {
        Ok(_dir) => {
            // TODO(jsankey): Bind this directory to a storage capability.
            info!("Successfully acquired an account directory");
        }
        Err(err) => {
            warn!("Error getting account directory: {:?}", err)
        }
    }

    let scene_manager = Arc::new(connect_to_protocol::<SceneManagerMarker>().unwrap());

    let shell_view_provider: ClientEnd<ViewProviderMarker> = view_provider
        .into_channel()
        .expect("no other users of the wrapped channel")
        .into_zx_channel()
        .into();
    let view_ref = scene_manager.set_root_view(shell_view_provider.into()).await.unwrap();

    let set_focus_fut = set_view_focus(Arc::downgrade(&scene_manager), view_ref);
    let focus_fut = input_pipeline::focus_listening::handle_focus_changes();

    let graphical_presenter = app.connect_to_protocol::<GraphicalPresenterMarker>()?;
    let element_manager = connect_to_protocol::<ElementManagerMarker>()?;
    let services_fut =
        expose_services(graphical_presenter, element_manager, ermine_services_server_end);

    //TODO(fxbug.dev/47080) monitor the futures to see if they complete in an error.
    let _ = try_join!(focus_fut, set_focus_fut, services_fut);

    Ok(())
}
