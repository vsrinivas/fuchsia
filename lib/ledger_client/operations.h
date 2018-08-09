// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The file defines Operations commonly executed on Ledger pages.

#ifndef PERIDOT_LIB_LEDGER_CLIENT_OPERATIONS_H_
#define PERIDOT_LIB_LEDGER_CLIENT_OPERATIONS_H_

#include <string>

#include <fuchsia/ledger/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include <lib/fidl/cpp/array.h>
#include <lib/fsl/vmo/strings.h>

#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/page_client.h"

namespace modular {

// Common base class for all Operation classes that operate on a ledger Page.
//
// The ledger page is always passed as a naked pointer fuchsia::ledger::Page*
// rather than as a FIDL pointer fuchsia::ledger::PagePtr to the Operation
// instance, because the connection to the page is shared between different
// Operation instances executed by different actors in the framework. The
// PagePtr is held by LedgerClient and handed out as Page* to PageClient, which
// passes it on to the respective Operations.
//
// As a result, callbacks for methods invoked on the Page* are not cancelled
// when the Operation instance is deleted, as they would be if the FIDL pointer
// were owned by the Operation instance. Therefore, such callbacks must be
// explicitly guarded using a weak pointer to the Operation instance against
// execution after their Operation instance was destroyed.
//
// This base class provides the method Protect() for the purpose.
//
// In derived class that are themselves template classes, the method must be
// invoked by explicit qualification with this-> (or alternatively with the base
// class name), because of template name lookup rules.
//
// Also, it is not possible to pass a PageSnapshotPtr to the Operation instead,
// because the snapshot must be taken at the time the Operation is executed, not
// at the time the Operation is enqueued, because it must reflect the effect of
// the execution of preceding operations.
template <typename... Args>
class PageOperation : public Operation<Args...> {
 public:
  using ResultCall = std::function<void(Args...)>;

  PageOperation(const char* const trace_name, fuchsia::ledger::Page* const page,
                ResultCall result_call, const std::string& trace_info = "")
      : Operation<Args...>(trace_name, std::move(result_call), trace_info),
        page_(page) {}

 protected:
  fuchsia::ledger::Page* page() const { return page_; }

  using PageCallback = std::function<void(fuchsia::ledger::Status)>;

  PageCallback Protect(PageCallback callback) {
    return [weak_this = this->GetWeakPtr(),
            callback = std::move(callback)](fuchsia::ledger::Status status) {
      if (weak_this) {
        callback(status);
      }
    };
  }

 private:
  fuchsia::ledger::Page* const page_;
};

// Like PageOperation, but also takes a naked Ledger pointer, which has the same
// problems.
//
// This could be unified more with PageOperation, but only worthwhile once there
// are more situations to support. For now, it's very nice to label Operation
// classes explicitly with their base classes.
template <typename... Args>
class LedgerOperation : public Operation<Args...> {
 public:
  using ResultCall = std::function<void(Args...)>;

  LedgerOperation(const char* const trace_name, fuchsia::ledger::Ledger* ledger,
                  fuchsia::ledger::Page* const page, ResultCall result_call,
                  const std::string& trace_info = "")
      : Operation<Args...>(trace_name, std::move(result_call), trace_info),
        ledger_(ledger),
        page_(page) {}

 protected:
  fuchsia::ledger::Ledger* ledger() const { return ledger_; }
  fuchsia::ledger::Page* page() const { return page_; }

  using LedgerCallback = std::function<void(fuchsia::ledger::Status)>;

  LedgerCallback Protect(LedgerCallback callback) {
    return [weak_this = this->GetWeakPtr(),
            callback = std::move(callback)](fuchsia::ledger::Status status) {
      if (weak_this) {
        callback(status);
      }
    };
  }

 private:
  fuchsia::ledger::Ledger* const ledger_;
  fuchsia::ledger::Page* const page_;
};

template <typename Data, typename DataPtr = std::unique_ptr<Data>,
          typename DataFilter = XdrFilterList<Data>>
class ReadDataCall : public PageOperation<DataPtr> {
 public:
  using ResultCall = std::function<void(DataPtr)>;
  using FlowToken = typename Operation<DataPtr>::FlowToken;

  ReadDataCall(fuchsia::ledger::Page* const page, const std::string& key,
               const bool not_found_is_ok, DataFilter filter,
               ResultCall result_call)
      : PageOperation<DataPtr>("ReadDataCall", page, std::move(result_call),
                               key),
        key_(key),
        not_found_is_ok_(not_found_is_ok),
        filter_(filter) {}

 private:
  void Run() override {
    FlowToken flow{this, &result_};

    this->page()->GetSnapshot(
        page_snapshot_.NewRequest(), fidl::VectorPtr<uint8_t>::New(0), nullptr,
        this->Protect([this, flow](fuchsia::ledger::Status status) {
          if (status != fuchsia::ledger::Status::OK) {
            FXL_LOG(ERROR) << this->trace_name() << " " << key_ << " "
                           << "Page.GetSnapshot() "
                           << fidl::ToUnderlying(status);
            return;
          }

          Cont(flow);
        }));
  }

  void Cont(FlowToken flow) {
    page_snapshot_->Get(to_array(key_), [this, flow](
                                            fuchsia::ledger::Status status,
                                            fuchsia::mem::BufferPtr value) {
      if (status != fuchsia::ledger::Status::OK) {
        if (status != fuchsia::ledger::Status::KEY_NOT_FOUND ||
            !not_found_is_ok_) {
          FXL_LOG(ERROR) << this->trace_name() << " " << key_ << " "
                         << "PageSnapshot.Get() " << fidl::ToUnderlying(status);
        }
        return;
      }

      if (!value) {
        FXL_LOG(ERROR) << this->trace_name() << " " << key_ << " "
                       << "PageSnapshot.Get() null vmo";
      }

      std::string value_as_string;
      if (!fsl::StringFromVmo(*value, &value_as_string)) {
        FXL_LOG(ERROR) << this->trace_name() << " " << key_
                       << " Unable to extract data.";
        return;
      }

      if (!XdrRead(value_as_string, &result_, filter_)) {
        result_.reset();
        return;
      }

      FXL_DCHECK(result_);
    });
  }

  const std::string key_;
  const bool not_found_is_ok_;
  DataFilter const filter_;
  fuchsia::ledger::PageSnapshotPtr page_snapshot_;
  DataPtr result_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ReadDataCall);
};

template <typename Data, typename DataArray = fidl::VectorPtr<Data>,
          typename DataFilter = XdrFilterList<Data>>
class ReadAllDataCall : public PageOperation<DataArray> {
 public:
  using ResultCall = std::function<void(DataArray)>;
  using FlowToken = typename Operation<DataArray>::FlowToken;

  ReadAllDataCall(fuchsia::ledger::Page* const page, std::string prefix,
                  DataFilter const filter, ResultCall result_call)
      : PageOperation<DataArray>("ReadAllDataCall", page,
                                 std::move(result_call), prefix),
        prefix_(std::move(prefix)),
        filter_(filter) {
    data_.resize(0);
  }

 private:
  void Run() override {
    FlowToken flow{this, &data_};

    this->page()->GetSnapshot(
        page_snapshot_.NewRequest(), to_array(prefix_), nullptr,
        this->Protect([this, flow](fuchsia::ledger::Status status) {
          if (status != fuchsia::ledger::Status::OK) {
            FXL_LOG(ERROR) << this->trace_name() << " "
                           << "Page.GetSnapshot() "
                           << fidl::ToUnderlying(status);
            return;
          }

          Cont1(flow);
        }));
  }

  void Cont1(FlowToken flow) {
    GetEntries(page_snapshot_.get(), &entries_,
               [this, flow](fuchsia::ledger::Status status) {
                 if (status != fuchsia::ledger::Status::OK) {
                   FXL_LOG(ERROR)
                       << this->trace_name() << " "
                       << "GetEntries() " << fidl::ToUnderlying(status);
                   return;
                 }

                 Cont2(flow);
               });
  }

  void Cont2(FlowToken /*flow*/) {
    for (auto& entry : entries_) {
      std::string value_as_string;
      if (!fsl::StringFromVmo(*entry.value, &value_as_string)) {
        FXL_LOG(ERROR) << this->trace_name() << " "
                       << "Unable to extract data.";
        continue;
      }

      Data data;
      if (!XdrRead(value_as_string, &data, filter_)) {
        continue;
      }

      data_.push_back(std::move(data));
    }
  }

  fuchsia::ledger::PageSnapshotPtr page_snapshot_;
  const std::string prefix_;
  DataFilter const filter_;
  std::vector<fuchsia::ledger::Entry> entries_;
  DataArray data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ReadAllDataCall);
};

template <typename Data, typename DataPtr = std::unique_ptr<Data>,
          typename DataFilter = XdrFilterList<Data>>
class WriteDataCall : public PageOperation<> {
 public:
  WriteDataCall(fuchsia::ledger::Page* const page, const std::string& key,
                DataFilter filter, DataPtr data, ResultCall result_call)
      : PageOperation("WriteDataCall", page, std::move(result_call), key),
        key_(key),
        filter_(filter),
        data_(std::move(data)) {}

 private:
  void Run() override {
    FlowToken flow{this};

    std::string json;
    XdrWrite(&json, &data_, filter_);

    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(json, &vmo));
    page()->CreateReferenceFromBuffer(
        std::move(vmo).ToTransport(),
        [this, weak_ptr = GetWeakPtr(), flow](
            fuchsia::ledger::Status status,
            std::unique_ptr<fuchsia::ledger::Reference> reference) {
          if (!weak_ptr) {
            return;
          }
          PutReference(std::move(reference), flow);
        });
  }

  void PutReference(std::unique_ptr<fuchsia::ledger::Reference> reference,
                    FlowToken flow) {
    if (!reference) {
      FXL_LOG(ERROR) << trace_name() << " " << key_ << " "
                     << "Page.Put() could not construct reference.";
      return;
    }

    page()->PutReference(
        to_array(key_), std::move(*reference), fuchsia::ledger::Priority::EAGER,
        Protect([this, flow](fuchsia::ledger::Status status) {
          if (status != fuchsia::ledger::Status::OK) {
            FXL_LOG(ERROR) << trace_name() << " " << key_ << " "
                           << "Page.Put() " << fidl::ToUnderlying(status);
          }
        }));
  }

  const std::string key_;
  DataFilter const filter_;
  DataPtr data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(WriteDataCall);
};

class DumpPageSnapshotCall : public PageOperation<std::string> {
 public:
  DumpPageSnapshotCall(fuchsia::ledger::Page* const page,
                       ResultCall result_call)
      : PageOperation("DumpPageSnapshotCall", page, std::move(result_call)) {}

 private:
  void Run() override {
    FlowToken flow{this, &dump_};

    page()->GetSnapshot(page_snapshot_.NewRequest(),
                        fidl::VectorPtr<uint8_t>::New(0), nullptr,
                        Protect([this, flow](fuchsia::ledger::Status status) {
                          if (status != fuchsia::ledger::Status::OK) {
                            FXL_LOG(ERROR) << this->trace_name() << " "
                                           << "Page.GetSnapshot() "
                                           << fidl::ToUnderlying(status);
                            return;
                          }

                          Cont1(flow);
                        }));
  }

  void Cont1(FlowToken flow) {
    GetEntries(page_snapshot_.get(), &entries_,
               [this, flow](fuchsia::ledger::Status status) {
                 if (status != fuchsia::ledger::Status::OK) {
                   FXL_LOG(ERROR)
                       << this->trace_name() << " "
                       << "GetEntries() " << fidl::ToUnderlying(status);
                   return;
                 }

                 Cont2(flow);
               });
  }

  void Cont2(FlowToken /*flow*/) {
    std::ostringstream stream;
    for (auto& entry : entries_) {
      stream << "key: " << to_hex_string(entry.key) << std::endl;

      std::string value_as_string;
      if (!fsl::StringFromVmo(*entry.value, &value_as_string)) {
        FXL_LOG(ERROR) << this->trace_name() << " "
                       << "Unable to extract data.";
        continue;
      }
      stream << "value: " << value_as_string << std::endl;
    }
    dump_ = stream.str();
  }

  fuchsia::ledger::PageSnapshotPtr page_snapshot_;
  std::vector<fuchsia::ledger::Entry> entries_;
  std::string dump_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DumpPageSnapshotCall);
};

}  // namespace modular

#endif  // PERIDOT_LIB_LEDGER_CLIENT_OPERATIONS_H_
