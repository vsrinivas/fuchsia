use application_services::{ApplicationController_Proxy};
use application_services_service_provider::ServiceProvider;
use apps_ledger_services_public::*;
use apps_ledger_services_internal::*;
use fuchsia::{Launcher, App};
use fidl::Error;
use magenta::{Vmo, self};

pub fn ledger_crash_callback(res: Result<Status, Error>) {
    let status = res.expect("ledger call failed to respond with a status");
    assert_eq!(status, Status_Ok, "ledger call failed");
}

pub struct Ledger {
    pub proxy: Ledger_Proxy,
    controller: LedgerController_Proxy,
    // Keeping this around keeps the app running
    _app_controller: ApplicationController_Proxy,
}

impl Ledger {
    /// This method of getting a ledger is only applicable to command line apps and should not be used in non-test programs
    pub fn new(launcher: &mut Launcher, repo_path: String, server_id: Option<String>, ledger_id: Vec<u8>) -> Ledger {
        let args = vec![String::from("--no_minfs_wait"), String::from("--no_persisted_config")];
        let mut app = launcher.launch(String::from("file:///system/apps/ledger"), Some(args));

        let (mut repo_factory, repo_factory_request) = LedgerRepositoryFactory_new_pair();
        app.services.connect_to_service(LedgerRepositoryFactory_Metadata::SERVICE_NAME.to_owned(),
            repo_factory_request.into_channel());
        let (controller, controller_request) = LedgerController_new_pair();
        app.services.connect_to_service(LedgerController_Metadata::SERVICE_NAME.to_owned(),
            controller_request.into_channel());

        let (mut repo, repo_request) = LedgerRepository_new_pair();
        repo_factory.get_repository(repo_path, server_id, None, repo_request);

        let (proxy, ledger_request) = Ledger_new_pair();
        repo.get_ledger(ledger_id, ledger_request).with(ledger_crash_callback);
        // TODO set connection error handler?

        let App { services: _, controller: app_controller } = app;
        Ledger { proxy, controller, _app_controller: app_controller }
    }
}

impl Drop for Ledger {
    fn drop(&mut self) {
        // I don't actually know if this is necessary or just closing the app_controller is sufficient
        self.controller.terminate();
    }
}

#[derive(Debug)]
pub enum ValueError {
    NeedsFetch,
    LedgerFail(Status),
    Vmo(magenta::Status),
}

pub fn value_result(res: (Status, Option<Vmo>)) -> Result<Option<Vec<u8>>, ValueError> {
    // Rust emits a warning if matched-on constants aren't all-caps
    const OK: Status = Status_Ok;
    const KEY_NOT_FOUND: Status = Status_KeyNotFound;
    const NEEDS_FETCH: Status = Status_NeedsFetch;
    match res {
        (OK, Some(vmo)) => {
            let size = vmo.get_size().map_err(ValueError::Vmo)?;
            // TODO: how fishy is this cast to usize?
            let mut buffer: Vec<u8> = Vec::with_capacity(size as usize);
            for _ in 0..size {
                buffer.push(0);
            }
            let bytes_read = vmo.read(buffer.as_mut_slice(), 0).map_err(ValueError::Vmo)?;
            buffer.truncate(bytes_read);
            Ok(Some(buffer))
        },
        (KEY_NOT_FOUND, _) => Ok(None),
        (NEEDS_FETCH, _) => Err(ValueError::NeedsFetch),
        (status, _) => Err(ValueError::LedgerFail(status)),
    }
}
