// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_CALLBACK_OBSERVER_LIST_H_
#define SRC_LEDGER_LIB_CALLBACK_OBSERVER_LIST_H_

// Derived from chromium/src/base/observer_list.h

#include <stddef.h>

#include <algorithm>
#include <limits>
#include <vector>

#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/memory/weak_ptr.h"

///////////////////////////////////////////////////////////////////////////////
//
// OVERVIEW:
//
//   A container for a list of observers.  Unlike a normal STL vector or list,
//   this container can be modified during iteration without invalidating the
//   iterator.  So, it safely handles the case of an observer removing itself
//   or other observers from the list while observers are being notified.
//
//
// THREAD-SAFETY:
//
//   ObserverList is not thread-safe. ObserverList objects must be created,
//   modified, accessed, and destroyed on the same thread.
//
//
// TYPICAL USAGE:
//
//   class MyWidget {
//    public:
//     ...
//
//     class Observer {
//      public:
//       virtual void OnFoo(MyWidget* w) = 0;
//       virtual void OnBar(MyWidget* w, int x, int y) = 0;
//     };
//
//     void AddObserver(Observer* obs) {
//       observer_list_.AddObserver(obs);
//     }
//
//     void RemoveObserver(Observer* obs) {
//       observer_list_.RemoveObserver(obs);
//     }
//
//     void NotifyFoo() {
//       for (auto& observer : observer_list_)
//         observer.OnFoo(this);
//     }
//
//     void NotifyBar(int x, int y) {
//       for (FooList::iterator i = observer_list.begin(),
//           e = observer_list.end(); i != e; ++i)
//        i->OnBar(this, x, y);
//     }
//
//    private:
//     ledger::ObserverList<Observer> observer_list_;
//   };
//
//
///////////////////////////////////////////////////////////////////////////////

namespace ledger {

template <class ObserverType>
class ObserverListBase {
 public:
  // Enumeration of which observers are notified.
  enum class NotifyWhat {
    // Specifies that any observers added during notification are notified.
    // This is the default type if non type is provided to the constructor.
    kAll,

    // Specifies that observers added while sending out notification are not
    // notified.
    kExistingOnly
  };

  // An iterator class that can be used to access the list of observers.
  template <class ContainerType>
  class Iter {
   public:
    Iter();
    explicit Iter(ContainerType* list);
    ~Iter();

    bool operator==(const Iter& other) const;
    bool operator!=(const Iter& other) const;
    Iter& operator++();
    ObserverType* operator->() const;
    ObserverType& operator*() const;

    // Methods for accessing the underlying container and current element. DO
    // NOT call these methods directly: these are public for testing only.
    const WeakPtr<ObserverListBase<ObserverType>>& GetContainer() { return list_; }
    ObserverType* GetCurrent() const;

   private:
    void EnsureValidIndex();

    size_t clamped_max_index() const { return std::min(max_index_, list_->observers_.size()); }

    bool is_end() const { return !list_ || index_ == clamped_max_index(); }

    WeakPtr<ObserverListBase<ObserverType>> list_;

    // When initially constructed and each time the iterator is incremented,
    // |index_| is guaranteed to point to a non-null index if the iterator
    // has not reached the end of the ObserverList.
    size_t index_;
    size_t max_index_;
  };

  using Iterator = Iter<ObserverListBase<ObserverType>>;

  using iterator = Iter<ObserverListBase<ObserverType>>;
  iterator begin() { return observers_.empty() ? iterator() : iterator(this); }
  iterator end() { return iterator(); }

  using const_iterator = Iter<const ObserverListBase<ObserverType>>;
  const_iterator begin() const {
    return observers_.empty() ? const_iterator() : const_iterator(this);
  }
  const_iterator end() const { return const_iterator(); }

  ObserverListBase() : notify_depth_(0), what_(NotifyWhat::kAll), weak_ptr_factory_(this) {}
  explicit ObserverListBase(NotifyWhat what)
      : notify_depth_(0), what_(what), weak_ptr_factory_(this) {}
  ObserverListBase(const ObserverListBase&) = delete;
  ObserverListBase& operator=(const ObserverListBase&) = delete;

  // Add an observer to the list.  An observer should not be added to
  // the same list more than once.
  void AddObserver(ObserverType* obs);

  // Remove an observer from the list if it is in the list.
  void RemoveObserver(ObserverType* obs);

  // Determine whether a particular observer is in the list.
  bool HasObserver(const ObserverType* observer) const;

  void Clear();

 protected:
  size_t size() const { return observers_.size(); }

  void Compact();

 private:
  using ListType = std::vector<ObserverType*>;

  WeakPtr<ObserverListBase> AsWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

  ListType observers_;
  int notify_depth_;
  NotifyWhat what_;

  template <class ContainerType>
  friend class Iter;

  WeakPtrFactory<ObserverListBase> weak_ptr_factory_;
};

template <class ObserverType>
template <class ContainerType>
ObserverListBase<ObserverType>::Iter<ContainerType>::Iter() : index_(0), max_index_(0) {}

template <class ObserverType>
template <class ContainerType>
ObserverListBase<ObserverType>::Iter<ContainerType>::Iter(ContainerType* list)
    : list_(const_cast<ObserverListBase<ObserverType>*>(list)->AsWeakPtr()),
      index_(0),
      max_index_(list->what_ == NotifyWhat::kAll ? std::numeric_limits<size_t>::max()
                                                 : list->observers_.size()) {
  EnsureValidIndex();
  LEDGER_DCHECK(list_);
  ++list_->notify_depth_;
}

template <class ObserverType>
template <class ContainerType>
ObserverListBase<ObserverType>::Iter<ContainerType>::~Iter() {
  if (list_ && --list_->notify_depth_ == 0)
    list_->Compact();
}

template <class ObserverType>
template <class ContainerType>
bool ObserverListBase<ObserverType>::Iter<ContainerType>::operator==(const Iter& other) const {
  if (is_end() && other.is_end())
    return true;
  return list_.get() == other.list_.get() && index_ == other.index_;
}

template <class ObserverType>
template <class ContainerType>
bool ObserverListBase<ObserverType>::Iter<ContainerType>::operator!=(const Iter& other) const {
  return !operator==(other);
}

template <class ObserverType>
template <class ContainerType>
typename ObserverListBase<ObserverType>::template Iter<ContainerType>&
ObserverListBase<ObserverType>::Iter<ContainerType>::operator++() {
  if (list_) {
    ++index_;
    EnsureValidIndex();
  }
  return *this;
}

template <class ObserverType>
template <class ContainerType>
ObserverType* ObserverListBase<ObserverType>::Iter<ContainerType>::operator->() const {
  ObserverType* current = GetCurrent();
  LEDGER_DCHECK(current);
  return current;
}

template <class ObserverType>
template <class ContainerType>
ObserverType& ObserverListBase<ObserverType>::Iter<ContainerType>::operator*() const {
  ObserverType* current = GetCurrent();
  LEDGER_DCHECK(current);
  return *current;
}

template <class ObserverType>
template <class ContainerType>
ObserverType* ObserverListBase<ObserverType>::Iter<ContainerType>::GetCurrent() const {
  if (!list_)
    return nullptr;
  return index_ < clamped_max_index() ? list_->observers_[index_] : nullptr;
}

template <class ObserverType>
template <class ContainerType>
void ObserverListBase<ObserverType>::Iter<ContainerType>::EnsureValidIndex() {
  if (!list_)
    return;

  size_t max_index = clamped_max_index();
  while (index_ < max_index && !list_->observers_[index_])
    ++index_;
}

template <class ObserverType>
void ObserverListBase<ObserverType>::AddObserver(ObserverType* obs) {
  LEDGER_DCHECK(obs);
  if (std::find(observers_.begin(), observers_.end(), obs) != observers_.end()) {
    LEDGER_NOTREACHED() << "Observers can only be added once!";
    return;
  }
  observers_.push_back(obs);
}

template <class ObserverType>
void ObserverListBase<ObserverType>::RemoveObserver(ObserverType* obs) {
  LEDGER_DCHECK(obs);
  typename ListType::iterator it = std::find(observers_.begin(), observers_.end(), obs);
  if (it != observers_.end()) {
    if (notify_depth_) {
      *it = nullptr;
    } else {
      observers_.erase(it);
    }
  }
}

template <class ObserverType>
bool ObserverListBase<ObserverType>::HasObserver(const ObserverType* observer) const {
  for (size_t i = 0; i < observers_.size(); ++i) {
    if (observers_[i] == observer)
      return true;
  }
  return false;
}

template <class ObserverType>
void ObserverListBase<ObserverType>::Clear() {
  if (notify_depth_) {
    for (typename ListType::iterator it = observers_.begin(); it != observers_.end(); ++it) {
      *it = nullptr;
    }
  } else {
    observers_.clear();
  }
}

template <class ObserverType>
void ObserverListBase<ObserverType>::Compact() {
  observers_.erase(std::remove(observers_.begin(), observers_.end(), nullptr), observers_.end());
}

template <class ObserverType, bool check_empty = false>
class ObserverList : public ObserverListBase<ObserverType> {
 public:
  using NotifyWhat = typename ObserverListBase<ObserverType>::NotifyWhat;

  ObserverList() = default;
  explicit ObserverList(NotifyWhat what) : ObserverListBase<ObserverType>(what) {}

  ~ObserverList() {
    // When check_empty is true, assert that the list is empty on destruction.
    if (check_empty) {
      ObserverListBase<ObserverType>::Compact();
      LEDGER_DCHECK(!might_have_observers());
    }
  }

  bool might_have_observers() const { return ObserverListBase<ObserverType>::size() != 0; }
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_CALLBACK_OBSERVER_LIST_H_
