// TODO: remove this
use mxruntime::{get_service_root, connect_to_environment_service};
use fidl::ClientEnd;
use garnet_public_lib_app_fidl::*;
use garnet_public_lib_app_fidl_service_provider::*;
use ledger::LedgerError;
use tokio_core::reactor;
#[cfg(target_arch = "x86_64")]
use std::panic;

/// Produces a proper backtrace on panic by triggering a software breakpoint
/// with a special value in a register that causes it to print a backtrace and
/// then resume execution, which then runs the old panic handler.
#[cfg(target_arch = "x86_64")]
pub fn install_panic_backtrace_hook() {
    let old_hook = panic::take_hook();
    panic::set_hook(Box::new(move |arg| {
        unsafe {
            // constant comes from CRASHLOGGER_RESUME_MAGIC in crashlogger.h
            asm!("mov $$0xee726573756d65ee, %rax; int3" : : : "rax" : "volatile");
        }
        old_hook(arg)
    }));
}

#[cfg(not(target_arch = "x86_64"))]
pub fn install_panic_backtrace_hook() {}

pub struct ApplicationContext {
    pub environment: ApplicationEnvironment::Proxy,
    pub environment_services: ServiceProvider::Proxy,
}

// pub type ApplicationContextPtr = Arc<Mutex<ApplicationContext>>;

impl ApplicationContext {
    pub fn new(handle: &reactor::Handle) -> Result<ApplicationContext, LedgerError> {
        let service_root = get_service_root().unwrap();
        let application_environment_channel =
            connect_to_environment_service(service_root,
                                           ApplicationEnvironment::SERVICE_NAME)
                .unwrap();
        let application_environment_client = ClientEnd::new(application_environment_channel);
        let mut proxy = ApplicationEnvironment::new_proxy(application_environment_client, handle)?;
        let (service_provider, service_provider_server) = ServiceProvider::new_pair(handle)?;
        proxy.get_services(service_provider_server);
        Ok(ApplicationContext {
            environment: proxy,
            environment_services: service_provider,
        })
    }
}

pub struct Launcher {
    proxy: ApplicationLauncher::Proxy,
}

impl Launcher {
    pub fn new(context: &mut ApplicationContext, handle: &reactor::Handle) -> Result<Launcher, LedgerError> {
        let (launcher_proxy, launcher_request) = ApplicationLauncher::new_pair(handle)?;
        context.environment_services.connect_to_service(ApplicationLauncher::SERVICE_NAME.to_owned(),
            launcher_request.into_channel());
        Ok(Launcher { proxy: launcher_proxy })
    }

    pub fn launch(&mut self, url: String, arguments: Option<Vec<String>>, handle: &reactor::Handle)
        -> Result<App, LedgerError>
    {
        let (services_proxy, services_request) = ServiceProvider::new_pair(handle)?;
        let (controller_proxy, controller_request) = ApplicationController::new_pair(handle)?;

        let launch_info = ApplicationLaunchInfo {
            url,
            arguments,
            service_request: None,
            flat_namespace: None,
            services: Some(services_request)
        };
        self.proxy.create_application(launch_info, Some(controller_request));
        Ok(App { services: services_proxy, controller: controller_proxy })
    }
}

pub struct App {
    pub services: ServiceProvider::Proxy,
    pub controller: ApplicationController::Proxy,
}
