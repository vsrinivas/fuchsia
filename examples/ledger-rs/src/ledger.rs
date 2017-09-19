use apps_ledger_services_public::*;
use apps_ledger_services_internal::*;
use fidl;
use fuchsia::{Launcher, App};
use garnet_public_lib_app_fidl::ApplicationController;
use garnet_public_lib_app_fidl_service_provider::ServiceProvider_I;
use tokio_core::reactor;
use futures::{future, Future};
use zircon::{Vmo, self};

macro_rules! try_to_fut {
    ($e:expr) => {
        match $e {
            Ok(x) => x,
            Err(e) => return Box::new(future::err(e.into())),
        }
    }
}

pub struct LedgerInstance {
    pub proxy: Ledger::Proxy,
    controller: LedgerController::Proxy,
    // Keeping this around keeps the app running
    _app_controller: ApplicationController::Proxy,
}

impl LedgerInstance {
    /// This method of getting a ledger is only applicable to command line apps and should not be used in non-test programs
    pub fn new(launcher: &mut Launcher, repo_path: String, ledger_id: Vec<u8>,
            handle: &reactor::Handle) -> Box<Future<Item = LedgerInstance, Error = LedgerError>>
    {
        let args = vec![String::from("--no_minfs_wait"), String::from("--no_persisted_config")];
        let ledger_url = String::from("file:///system/apps/ledger");
        let mut app = try_to_fut!(launcher.launch(ledger_url, Some(args), handle));

        let (mut repo_factory, repo_factory_request) = try_to_fut!(LedgerRepositoryFactory::new_pair(handle));
        app.services.connect_to_service(LedgerRepositoryFactory::SERVICE_NAME.to_owned(),
            repo_factory_request.into_channel());
        let (controller, controller_request) = try_to_fut!(LedgerController::new_pair(handle));
        app.services.connect_to_service(LedgerController::SERVICE_NAME.to_owned(),
            controller_request.into_channel());

        let (mut repo, repo_request) = try_to_fut!(LedgerRepository::new_pair(handle));
        repo_factory.get_repository(repo_path, None, None, repo_request);

        let (proxy, ledger_request) = try_to_fut!(Ledger::new_pair(handle));
        Box::new(repo.get_ledger(ledger_id, ledger_request)
            .then(map_ledger_error)
            .map(move |()| {
                // TODO set connection error handler?

                let App { services: _, controller: app_controller } = app;
                LedgerInstance { proxy, controller, _app_controller: app_controller }
            }))
    }
}

impl Drop for LedgerInstance {
    fn drop(&mut self) {
        // I don't actually know if this is necessary or just closing the app_controller is sufficient
        self.controller.terminate();
    }
}

#[allow(non_upper_case_globals)]
pub fn map_ledger_error(res: Result<Status, fidl::Error>) -> Result<(), LedgerError> {
    match res {
        Ok(Status_Ok) => Ok(()),
        Ok(status) => Err(LedgerError::LedgerFail(status)),
        Err(e) => Err(LedgerError::FidlError(e)),
    }
}

#[derive(Debug)]
pub enum LedgerError {
    NeedsFetch,
    LedgerFail(Status),
    Vmo(zircon::Status),
    FidlError(fidl::Error),
}

impl From<fidl::Error> for LedgerError {
    fn from(err: fidl::Error) -> Self {
        LedgerError::FidlError(err)
    }
}

pub fn map_value_result(res: Result<(Status, Option<Vmo>), fidl::Error>)
    -> Result<Option<Vec<u8>>, LedgerError>
{
    // Rust emits a warning if matched-on constants aren't all-caps
    const OK: Status = Status_Ok;
    const KEY_NOT_FOUND: Status = Status_KeyNotFound;
    const NEEDS_FETCH: Status = Status_NeedsFetch;
    match res {
        Ok((OK, Some(vmo))) => {
            let size = vmo.get_size().map_err(LedgerError::Vmo)?;
            // TODO: how fishy is this cast to usize?
            let mut buffer: Vec<u8> = Vec::with_capacity(size as usize);
            for _ in 0..size {
                buffer.push(0);
            }
            let bytes_read = vmo.read(buffer.as_mut_slice(), 0).map_err(LedgerError::Vmo)?;
            buffer.truncate(bytes_read);
            Ok(Some(buffer))
        },
        Ok((KEY_NOT_FOUND, _)) => Ok(None),
        Ok((NEEDS_FETCH, _)) => Err(LedgerError::NeedsFetch),
        Ok((status, _)) => Err(LedgerError::LedgerFail(status)),
        Err(e) => Err(LedgerError::FidlError(e)),
    }
}
