// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIDL_JSON_XDR_H_
#define PERIDOT_LIB_FIDL_JSON_XDR_H_

#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include <lib/fidl/cpp/array.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "rapidjson/document.h"

namespace modular {

// This file provides a tool to serialize arbitrary data structures
// into JSON, and back. It specifically supports serialization of FIDL
// data (structs, arrays, maps, and combinations thereof), but FIDL is
// not a requirement. For example, support for STL containers in
// addition to FIDL containers is easy to add once we need it.
//
// We use JSON as the serialization format to store structured values
// (and at times also structured keys) in the ledger.
//
// The design is inspired by Sun RPC's XDR, specifically the definiton
// of "filters". A filter function takes an operation and a data
// pointer, and depending on the operation parameter either serializes
// or deserializes the data. There is one such filter function for
// every data type. A filter for a simple data type does different
// things for serialization and deserialization, so having a single
// one for both operations instead of two separate functions barely
// reduces code size. However, the efficiency of this design shows in
// composition: A filter for a struct can be written by simply calling
// the filters for each field of the struct and passing the operation
// parameter down. Thus, a filter function for a struct is half the
// code size of a pair of serialization/deserialization functions.
//
// NOTES:
//
// XDR is not sync: Although the XDR operation can be applied to an
// existing instance of the output end (an existing FIDL struct, or an
// existing JSON AST), full synchronization of the data structure is
// not guaranteed. All data that exist in the input are added to the
// output, but not necessarily all data that don't exist in the input
// are removed from the output. Also, if an error occurs, the output
// is left in some intermediate state. The most suitable use for
// updates as of now is to always create a fresh output instance, and
// if the transciption succeeds, replace the previous instance by the
// fresh instance.
//
// XDR is not about resolving conflicts: If an existing output
// instance is updated using XDR, we might improve accuracy of
// removing data that no longer exist, but it is out of the scope of
// XDR (at least for now) to note that input data conflict with
// existing output data, and resolving the conflict. Conflict
// resolution between different versions of data is most likely
// handled outside XDR.
//
// It may be that we will use XDR to support conflict resolution in a
// data type agnostic way: Instead of defining a conflict resolution
// between e.g. STL or FIDL data structures, we might instead define
// XDR filters for them, translate all values to JSON, apply conflict
// resolution to JSON, and translate the result back.
//
// SCHEMA VERSION BACK COMPATIBILITY:
//
// The schema of the persistent data is defined in terms of filter functions. In
// order to support new versions of the code reading versions of the data
// written by old versions of the code, filter functions are always defined by
// the client at the top level entry points as lists, never as single functions.
//
// The lists contain the filter for the current version of the schema at the
// top, and filters for reading previous versions into the current version of
// the code below.
//
// Whenever the storage schema changes, a new version of the filter is created
// and add it to the version list.
//
// If the memory schema changes, filters of all versions are adjusted as
// necessary.
//
// Filters that don't change can be reused between versions. If a filter does
// not change, but the ones that it uses do change, templates can be used to
// save on code duplication.
//
// TODO(mesch): Right now there is no way to ensure that old versions of the
// code will never read new versions of the data. Support for this is expected
// from the Ledger, and partially from an upcoming API for explicit version
// numbers.
//
// See comments on XdrFilterType, XdrRead(), and XdrWrite() for details.

class XdrContext;

// The two operations: reading from JSON or writing to JSON.
enum class XdrOp {
  TO_JSON = 0,
  FROM_JSON = 1,
};

// Custom types are serialized by passing a function of this type to a method on
// XdrContext. Note this is a pointer type that points to a const (an existing
// function). So we will never use a reference or a const of it. However,
// argument values of such will still be defined const.
//
// The top level entry functions used by clients never pass a single filter
// function alone, but always a list of filters for different versions of the
// data, such that the reading code can fall back to functions reading
// previously written versions. Such lists can (and should) be defined
// statically as constexpr:
//
//   void XdrFoo_v1(XdrContext* const xdr, Foo* const data) { ... }
//   void XdrFoo_v2(XdrContext* const xdr, Foo* const data) { ... }
//
//   constexpr XdrFilterType<Foo> XdrFoo[] = {
//     XdrFoo_v1,
//     XdrFoo_v2,
//     nullptr,
//   };
//
//   Foo foo;
//   XdrRead(json, &foo, XdrFoo);
//
// XdrRead and XdrWrite are defined to take an XdrFilterList<> argument, which
// that constexpr can be passed to. See Definition of XdrRead(), below.
template <typename T>
using XdrFilterType = void (*)(XdrContext*, T*);

template <typename T>
using XdrFilterList = void (*const*)(XdrContext*, T*);

// A generic implementation of such a filter, which works for simple
// types. (The implementation uses XdrContext, so it's below.)
template <typename V>
void XdrFilter(XdrContext* xdr, V* value);

// XdrContext holds on to a JSON document as well as a specific
// position inside the document on which its methods operate, as well
// as the operation (writing to JSON, reading from JSON) that is
// executed when methods are called.
//
// There are two kinds of methods: Value() and Field(). Value()
// affects the current JSON value itself. Field() assumes the current
// JSON value is an Object, accesses a property on it and affects the
// value of the property.
//
// Clients usually call Value(); filters for custom types usually call
// Field().
class XdrContext {
 public:
  XdrContext(XdrOp op, JsonDoc* doc, std::string* error);

  ~XdrContext();

  // Returns the XdrOp that this XdrContext was created with.
  //
  // This is required by some XdrFilters that cannot use the same code to set or
  // get data from objects. However, in general, try to avoid special-casing
  // an XdrFilter to change behavior based on whether it's translating to or
  // from JSON.
  XdrOp op() const { return op_; }

  // Below are methods to handle values on properties of objects for
  // handling standalone values. These methods are called by filter
  // code during a serialization/deserialization operation.

  // The version of a struct. On Write, the version number is written and it
  // always returns true. On Read, raises an error and returns false if the
  // version number read doesn't match the version number passed in. Thus it
  // gives an explicit way to a filter function to force an error.
  //
  // The filter should also return early so as to not partially read data.
  //
  // This can be applied at any level, but only when it happens as the first
  // call in the top level filter will it fully prevent partial reads.
  //
  // How it should be used:
  //
  //   void XdrFoo_v1(XdrContext* const xdr, Foo* const data) { ... }
  //   void XdrFoo_v2(XdrContext* const xdr, Foo* const data) {
  //      if (!xdr->Version(2)) {
  //        return;
  //      }
  //      ...
  //   }
  //
  //   constexpr XdrFilterType<Foo> XdrFoo[] = {
  //     XdrFoo_v2,
  //     XdrFoo_v1,
  //     nullptr,
  //   };
  //
  //   Foo foo;
  //   XdrRead(json, &foo, XdrFoo);
  //
  // Notice that _v1 doesn't need to have a Version() call. This is usual when
  // the first use of the data predates the introduction of the Version()
  // mechanism.
  //
  // This method cannot be used (and returns false and logs an error) in a
  // context that is not Object.
  //
  // It writes the reserved field name "@version" to the current Object context.
  //
  // The value passed to the call inside the Xdr filter function should never be
  // defined as a constant outside of the filter function, because then it
  // becomes tempting to change it to a new version number without creating a
  // copy of the filter function for the previous version number.
  bool Version(uint32_t version);

  // A field of a struct. The value type V is assumed to be one of the
  // primitive JSON data types. If anything else is passed here and
  // not to the method below with a custom filter, the rapidjson code
  // will fail to compile.
  template <typename V>
  void Field(const char field[], V* const data) {
    Field(field).Value(data);
  }

  // If we supply a custom filter for the value of a field, the data
  // type of the field very often does not match directly the data
  // type for which we write a filter, therefore this template has two
  // type parameters. This happens in several situations:
  //
  // 1. Fields with fidl struct types. The field data type, which we pass the
  //    data for, is a std::unique_ptr<X>, but the filter supplied is for X (and
  //    thus takes X*).
  //
  // 2. Fields with fidl array types. The filter is for an element,
  //    but the field is the array type.
  //
  // 3. Fields with STL container types. The filter is for an element,
  //    but the field is the container type.
  //
  // We could handle this by specialization, it's much simpler to just cover all
  // possible combinations with a template of higher dimension, at the expense
  // of covering also a few impossible cases.
  template <typename D, typename V>
  void Field(const char field[], D* const data, XdrFilterType<V> const filter) {
    Field(field).Value(data, filter);
  }

  // Below are methods analog to those for values on properties of
  // objects for handling standalone values. These methods are called
  // by XdrContext client code such as XdrRead() and XdrWrite() to
  // start a serialization/deserialization operation.

  // A simple value is mapped to the corresponding JSON type (int,
  // float, bool) directly.
  template <typename V>
  typename std::enable_if<!std::is_enum<V>::value>::type Value(V* data) {
    switch (op_) {
      case XdrOp::TO_JSON:
        value_->Set(*data, allocator());
        break;

      case XdrOp::FROM_JSON:
        if (!value_->Is<V>()) {
          AddError("Unexpected type.");
          return;
        }
        *data = value_->Get<V>();
    }
  }

  // An enum is mapped to a JSON int.
  template <typename V>
  typename std::enable_if<std::is_enum<V>::value>::type Value(V* const data) {
    switch (op_) {
      case XdrOp::TO_JSON:
        value_->Set(static_cast<int>(*data), allocator());
        break;

      case XdrOp::FROM_JSON:
        if (!value_->Is<int>()) {
          AddError("Unexpected type.");
          return;
        }
        *data = static_cast<V>(value_->Get<int>());
    }
  }

  // Bytes and shorts, both signed and unsigned, are mapped to JSON int, since
  // they are not directly supported in the rapidjson API.
  void Value(unsigned char* data);
  void Value(int8_t* data);
  void Value(unsigned short* data);
  void Value(short* data);

  // A fidl String is mapped to either (i.e., the union type of) JSON
  // null or JSON string.
  void Value(fidl::StringPtr* data);

  // An STL string is mapped to a JSON string.
  void Value(std::string* data);

  // A value of a custom type is mapped using the custom filter. See
  // the corresponding Field() method for why there are two type
  // parameters here.
  template <typename D, typename V>
  void Value(D* data, XdrFilterType<V> filter) {
    filter(this, data);
  }

  // Operator & may be overloaded to return a type that acts like a pointer, but
  // isn't one, and therefore is not matched by the Value<D,V>(data, filter)
  // method above. In that case, we need to exercise the operator * of the
  // pointer type explicitly.
  //
  // This is needed for example for std::vector<bool>, where &at(i) is a bit
  // iterator, not a bool*.
  template <typename Ptr, typename V>
  void Value(Ptr data, XdrFilterType<V> filter) {
    switch (op_) {
      case XdrOp::TO_JSON: {
        V value = *data;
        filter(this, &value);
        break;
      }

      case XdrOp::FROM_JSON: {
        V value;
        filter(this, &value);
        *data = std::move(value);
      }
    }
  }

  template <typename S>
  void Value(std::unique_ptr<S>* data, XdrFilterType<S> filter) {
    switch (op_) {
      case XdrOp::TO_JSON:
        if (!data->get()) {
          value_->SetNull();
        } else {
          value_->SetObject();
          filter(this, data->get());
        }
        break;

      case XdrOp::FROM_JSON:
        if (value_->IsNull()) {
          data->reset();
        } else {
          if (!value_->IsObject()) {
            AddError("Object type expected.");
            return;
          }

          *data = std::make_unique<S>();
          filter(this, data->get());
        }
    }
  }

  // A fidl vector is mapped to JSON null and JSON Array with a custom
  // filter for the elements.
  template <typename D, typename V>
  void Value(fidl::VectorPtr<D>* const data, const XdrFilterType<V> filter) {
    switch (op_) {
      case XdrOp::TO_JSON:
        if (data->is_null()) {
          value_->SetNull();

        } else {
          value_->SetArray();
          value_->Reserve((*data)->size(), allocator());

          for (size_t i = 0; i < (*data)->size(); ++i) {
            Element(i).Value(&(*data)->at(i), filter);
          }
        }
        break;

      case XdrOp::FROM_JSON:
        if (value_->IsNull()) {
          data->reset();

        } else {
          if (!value_->IsArray()) {
            AddError("Array type expected.");
            return;
          }

          // The resize() call has two purposes:
          //
          // (1) Setting data to non-null, even if there are only zero
          //     elements. This is essential, otherwise the FIDL output
          //     is wrong (i.e., the FIDL output cannot be used in FIDL
          //     method calls without crashing).
          //
          // (2) It saves on allocations for growing the underlying
          //     vector one by one.
          data->resize(value_->Size());

          for (size_t i = 0; i < value_->Size(); ++i) {
            Element(i).Value(&(*data)->at(i), filter);
          }
        }
    }
  }

  // A fidl array with a simple element type can infer its element
  // value filter from the type parameters of the array.
  template <typename V>
  void Value(fidl::VectorPtr<V>* const data) {
    Value(data, XdrFilter<V>);
  }

  // A fidl array is mapped to JSON null and JSON Array with a custom
  // filter for the elements.
  template <typename D, size_t N, typename V>
  void Value(fidl::Array<D, N>* const data, const XdrFilterType<V> filter) {
    switch (op_) {
      case XdrOp::TO_JSON: {
        value_->SetArray();
        value_->Reserve(N, allocator());

        for (size_t i = 0; i < N; ++i) {
          Element(i).Value(&data->at(i), filter);
        }
        break;
      }

      case XdrOp::FROM_JSON: {
        if (!value_->IsArray()) {
          AddError("Array type expected.");
          return;
        }

        if (value_->Size() != N) {
          AddError(std::string("Array size unexpected: found ") +
                   std::to_string(value_->Size()) + " expected " +
                   std::to_string(N));
          return;
        }

        for (size_t i = 0; i < N; ++i) {
          Element(i).Value(&data->at(i), filter);
        }
      }
    }
  }

  // A fidl array with a simple element type can infer its element
  // value filter from the type parameters of the array.
  template <typename V, size_t N>
  void Value(fidl::Array<V, N>* const data) {
    Value(data, XdrFilter<V>);
  }

  // An STL vector is mapped to JSON Array with a custom filter for the
  // elements.
  template <typename D, typename V>
  void Value(std::vector<D>* const data, const XdrFilterType<V> filter) {
    switch (op_) {
      case XdrOp::TO_JSON:
        value_->SetArray();
        value_->Reserve(data->size(), allocator());

        for (size_t i = 0; i < data->size(); ++i) {
          Element(i).Value(&data->at(i), filter);
        }
        break;

      case XdrOp::FROM_JSON:
        if (!value_->IsArray()) {
          AddError("Array type expected.");
          return;
        }

        data->resize(value_->Size());

        for (size_t i = 0; i < value_->Size(); ++i) {
          Element(i).Value(&data->at(i), filter);
        }
    }
  }

  // An STL vector with a simple element type can infer its element value filter
  // from the type parameters of the array.
  template <typename V>
  void Value(std::vector<V>* const data) {
    Value(data, XdrFilter<V>);
  }

  // An STL map is mapped to an array of pairs of key and value, because maps
  // can have non-string keys. There are two filters, for the key type and the
  // value type.
  template <typename K, typename V>
  void Value(std::map<K, V>* const data, XdrFilterType<K> const key_filter,
             XdrFilterType<V> const value_filter) {
    switch (op_) {
      case XdrOp::TO_JSON: {
        value_->SetArray();
        value_->Reserve(data->size(), allocator());

        size_t index = 0;
        for (auto i = data->begin(); i != data->end(); ++i) {
          XdrContext&& element = Element(index++);
          element.value_->SetObject();

          K k{i->first};
          element.Field("@k").Value(&k, key_filter);

          V v{i->second};
          element.Field("@v").Value(&v, value_filter);
        }
        break;
      }

      case XdrOp::FROM_JSON: {
        if (!value_->IsArray()) {
          AddError("Array type expected.");
          return;
        }

        // Erase existing data in case there are some left.
        data->clear();

        size_t index = 0;
        for (auto i = value_->Begin(); i != value_->End(); ++i) {
          XdrContext&& element = Element(index++);

          K k;
          element.Field("@k").Value(&k, key_filter);

          V v;
          element.Field("@v").Value(&v, value_filter);

          data->emplace(std::move(k), std::move(v));
        }
      }
    }
  }

  // An STL map with only simple values can infer its key value filters from the
  // type parameters of the map.
  template <typename K, typename V>
  void Value(std::map<K, V>* const data) {
    Value(data, XdrFilter<K>, XdrFilter<V>);
  }

 private:
  XdrContext(XdrContext* parent, const char* name, XdrOp op, JsonDoc* doc,
             JsonValue* value);
  JsonDoc::AllocatorType& allocator() const { return doc_->GetAllocator(); }
  XdrContext Field(const char field[]);
  XdrContext Element(size_t i);

  // Error reporting: Recursively requests the error string from the
  // parent, and on the way back appends a description of the current
  // JSON context hierarchy.
  void AddError(const std::string& message);
  std::string* AddError();

  // Return the root error string so that IgnoreError() can manipulate it.
  std::string* GetError();

  // The root of the context tree (where parent_ == nullptr) keeps a
  // string to write errors to. In an error situation the chain of
  // parent contexts is traversed up in order to (1) access the error
  // string to write to, (2) record the current context hierarchy in
  // an error message. Each level in the context hierarchy is
  // described using the type of value_ and, if present, name_. name_
  // is the name of the field for contexts that are values of a field,
  // otherwise nullptr.
  XdrContext* const parent_;
  const char* const name_;
  std::string* const error_;

  // These three fields represent the context itself: The operation to
  // perform (read or write), the value it will be performed on, and
  // the document the value is part of, in order to access the
  // allocator.
  const XdrOp op_;
  JsonDoc* const doc_;
  JsonValue* const value_;

  // A JSON value to continue processing on when the expected one is
  // not found in the JSON AST, to avoid value_ becoming null. It
  // needs to be thread local because it is a global that's modified
  // potentially by every ongoing XDR invocation.
  static thread_local JsonValue null_;

  // All Xdr* functions take a XdrContext* and pass it on. We might
  // want to change this once we support asynchronous input/output
  // operations, for example directly to/from a Ledger page rather
  // than just the JSON DOM.
  FXL_DISALLOW_COPY_AND_ASSIGN(XdrContext);
};

// This filter function works for all types for which XdrContext has a Value()
// method defined.
template <typename V>
void XdrFilter(XdrContext* const xdr, V* const value) {
  xdr->Value(value);
}

// Clients use the following functions as entry points.

// A wrapper function to read data from a JSON document. This may fail if the
// JSON document doesn't match the structure required by any of the filter
// versions. In that case it logs an error and returns false. Clients are
// expected to either crash or recover e.g. by ignoring the value.
//
// The items in the filter versions list are tried in turn until one succeeds.
// The filter versions list must end with a nullptr entry to mark the end.
template <typename D, typename V>
bool XdrRead(JsonDoc* const doc, D* const data,
             XdrFilterList<V> filter_versions) {
  std::vector<std::string> errors;
  for (XdrFilterList<V> filter = filter_versions; *filter; ++filter) {
    std::string error;
    XdrContext xdr(XdrOp::FROM_JSON, doc, &error);
    xdr.Value(data, *filter);

    if (error.empty()) {
      return true;
    }

    FXL_LOG(INFO) << "Filter failed, trying previous version.";
    errors.emplace_back(std::move(error));
  }

  FXL_LOG(ERROR) << "XdrRead: No filter version succeeded"
                 << " to extract data from JSON: "
                 << JsonValueToPrettyString(*doc) << std::endl;
  for (const std::string& error : errors) {
    FXL_LOG(INFO) << "XdrRead error message: " << error;
  }

  return false;
}

// A wrapper function to read data from a JSON string. This may fail if the JSON
// doesn't parse or doesn't match the structure required by the filter version
// list. In that case it logs an error and returns false. Clients are expected
// to either crash or recover e.g. by ignoring the value.
template <typename D, typename V>
bool XdrRead(const std::string& json, D* const data,
             XdrFilterList<V> filter_versions) {
  JsonDoc doc;
  doc.Parse(json);
  if (doc.HasParseError()) {
    FXL_LOG(ERROR) << "Unable to parse data as JSON: " << json;
    return false;
  }

  return XdrRead(&doc, data, filter_versions);
}

// A wrapper function to write data as JSON doc. This never fails. It always
// only uses the first version of the filter. It takes a filter version list
// anyway for symmetry with XdrRead(), so that the same filter version list
// constant can be passed to both XdrRead and XdrWrite.
template <typename D, typename V>
void XdrWrite(JsonDoc* const doc, D* const data,
              XdrFilterList<V> filter_versions) {
  std::string error;
  XdrContext xdr(XdrOp::TO_JSON, doc, &error);
  xdr.Value(data, filter_versions[0]);
  FXL_DCHECK(error.empty())
      << "There are no errors possible in XdrOp::TO_JSON: " << std::endl
      << error << std::endl
      << JsonValueToPrettyString(*doc) << std::endl;
}

// A wrapper function to write data as JSON to a string. This never fails.
template <typename D, typename V>
void XdrWrite(std::string* const json, D* const data,
              XdrFilterList<V> filter_versions) {
  JsonDoc doc;
  XdrWrite(&doc, data, filter_versions);
  *json = JsonValueToString(doc);
}

// A wrapper function to return data as a JSON string. This never fails.
template <typename D, typename V>
std::string XdrWrite(D* const data, XdrFilterList<V> filter_versions) {
  std::string json;
  XdrWrite(&json, data, filter_versions);
  return json;
}

}  // namespace modular

#endif  // PERIDOT_LIB_FIDL_JSON_XDR_H_
