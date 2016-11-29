
#include "apps/modular/src/user_runner/user_ledger_repository_factory.h"

#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/logging.h"

namespace modular {

UserLedgerRepositoryFactory::UserLedgerRepositoryFactory(
  const std::string& user_repository_path,
  ledger::LedgerRepositoryFactoryPtr ledger_repository_factory)
    : user_repository_path_(user_repository_path),
      ledger_repository_factory_(std::move(ledger_repository_factory)) {}

ledger::LedgerRepositoryPtr UserLedgerRepositoryFactory::Clone() {
  ledger::LedgerRepositoryPtr repo;
  ledger_repository_factory_->GetRepository(
        user_repository_path_, GetProxy(&repo),
        [this](ledger::Status status) {
          if (status != ledger::Status::OK) {
            // TODO(vardhan): Make LedgerStatusToString() available.
            FTL_LOG(ERROR)
                << "UserLedgerRepositoryFactory: "
                << "LedgerRepositoryFactory.GetRepository() failed: "
                << status;
          }
        });
  return repo;
}

}  // namespace modular
