// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <fbl/algorithm.h>
#include <fbl/atomic.h>

#include <lockdep/common.h>
#include <lockdep/lock_dependency_set.h>
#include <lockdep/lock_traits.h>

namespace lockdep {

// Type that holds the essential information and state for a lock class. This is
// used by ThreadLockState to uniformly operate on the variety of lock classes
// created by each template instantiation of LockClass. Each template
// instantiation of LockClass creates a unique static instance of LockClassState.
class LockClassState {
public:
    // Constructs an instance of LockClassState.
    LockClassState(const char* const name,
                   LockDependencySet* dependency_set,
                   LockFlags flags)
        : name_{name},
          dependency_set_{dependency_set},
          flags_{flags} {}

    // Disable copy construction / assignment.
    LockClassState(const LockClassState&) = delete;
    LockClassState& operator=(const LockClassState&) = delete;

    // Retruns the LockClassState instance for the given lock class id. The id must
    // be a valid lock class id.
    static LockClassState* Get(LockClassId id) {
        return reinterpret_cast<LockClassState*>(id);
    }

    // Returns the type name of the lock class for the given lock class id.
    static const char* GetName(LockClassId id) { return Get(id)->name_; }

    // Returns true if lock class given by |search_id| is in the dependency set of
    // the lock class given by |id|, false otherwise.
    static bool HasLockClass(LockClassId id, LockClassId search_id) {
        return Get(id)->dependency_set_->HasLockClass(search_id);
    }

    // Adds the lock class given by |add_id| to the dependency set of the lock
    // class given by |id|.
    static LockResult AddLockClass(LockClassId id, LockClassId add_id) {
        return Get(id)->dependency_set_->AddLockClass(add_id);
    }

    // Returns true if the given lock class is irq-safe, false otherwise.
    static bool IsIrqSafe(LockClassId id) {
        return !!(Get(id)->flags_ & LockFlagsIrqSafe);
    }

    // Returns true if the given lock class is nestable, false otherwise.
    static bool IsNestable(LockClassId id) {
        return !!(Get(id)->flags_ & LockFlagsNestable);
    }

    // Returns true if reporting is disabled for the given lock class, false
    // otherwise.
    static bool IsReportingDisabled(LockClassId id) {
        return !!(Get(id)->flags_ & LockFlagsReportingDisabled);
    }

    // Returns true if the validator should abort the program if it detects an
    // invalid re-acquire with this lock class.
    static bool IsReAcquireFatal(LockClassId id) {
        return !!(Get(id)->flags_ & LockFlagsReAcquireFatal);
    }

    // Returns true if the lock should not be added to the active lock list
    // during an acquire.
    static bool IsActiveListDisabled(LockClassId id) {
      return !!(Get(id)->flags_ & LockFlagsActiveListDisabled);
    }

    // Returns true if the lock should not be tracked.
    static bool IsTrackingDisabled(LockClassId id) {
      return !!(Get(id)->flags_ & LockFlagsTrackingDisabled);
    }

    // Iterator type to traverse the set of LockClassState instances.
    class Iterator {
    public:
        Iterator() = default;
        Iterator(const Iterator&) = default;
        Iterator& operator=(const Iterator&) = default;

        LockClassState& operator*() { return *state_; }
        Iterator operator++() {
            state_ = state_->next_;
            return *this;
        }
        bool operator!=(const Iterator& other) const {
            return state_ != other.state_;
        }

        Iterator begin() { return {*Head()}; }
        Iterator end() { return {nullptr}; }

    private:
        Iterator(LockClassState* state)
            : state_{state} {}
        LockClassState* state_{nullptr};
    };

    // Returns an iterator for the init-time linked list of state instances.
    static Iterator Iter() { return {}; }

    // Returns the lock class id for this instance. The id is the address of the
    // instance.
    LockClassId id() const { return reinterpret_cast<LockClassId>(this); }

    // Returns the name of this lock class.
    const char* name() const { return name_; }

    // Return the flags of this lock class.
    LockFlags flags() const { return flags_; }

    // Returns the dependency set for this lock class.
    const LockDependencySet& dependency_set() const { return *dependency_set_; }

    LockClassState* connected_set() { return LoopDetector::FindSet(&loop_node_)->ToState(); }

    // Runs a loop detection pass on the set of lock classes to find possible
    // circular lock dependencies.
    static void LoopDetectionPass() {
        static LoopDetector detector;
        detector.DetectionPass();
    }

    uint64_t index() const { return loop_node_.index; }
    uint64_t least() const { return loop_node_.least; }

    // Resets the dependency set and disjoint set of this object. This is
    // primarily used to initialize the state between successive tests.
    void Reset() {
        dependency_set_->clear();
        loop_node_.Reset();
    }

private:
    // The name of the lock class type.
    const char* const name_;

    // The set of out edges from this node in the lock class dependency graph.
    // Out edges represent lock classes that have been held before this class.
    LockDependencySet* const dependency_set_;

    // Flags specifying which which rules to apply during lock validation.
    const LockFlags flags_;

    // Linked list pointer to the next state instance. This list is constructed
    // by a global initializer and never modified again. The list is used by the
    // loop detector and runtime lock inspection commands to access the complete
    // list of lock classes.
    LockClassState* next_{InitNext(this)};

    // Returns a pointer to the head pointer of the state linked list.
    static LockClassState** Head() {
        static LockClassState* head{nullptr};
        return &head;
    }

    // Updates the linked list to include the given state node and returns the
    // previous head. This is used by the global initializer to setup the
    // next_ member.
    static LockClassState* InitNext(LockClassState* state) {
        LockClassState* head = *Head();
        *Head() = state;
        return head;
    }

    // Per-lock class state used by the loop detection algorithm.
    struct LoopNode {
        // The parent of the disjoint sets this node belongs to. Nodes start out
        // alone in their own set. Sets are joined by the loop detector when
        // found within a cycle.
        fbl::atomic<LoopNode*> parent{this};

        // Linked list node for the loop detector's active node stack. The use
        // of a linked list of statically allocated nodes avoids dynamic memory
        // allocation during graph traversal.
        LoopNode* next{nullptr};

        // Index values used by the loop detector algorithm.
        uint64_t index{0};
        uint64_t least{0};

        // Returns a pointer to the LockClassState instance that contains this
        // LoopNode. This allows the loop detector to mostly operate in terms
        // of LoopNode instances, simplfying expressions in the main algorithm.
        LockClassState* ToState() {
            static_assert(fbl::is_standard_layout<LockClassState>::value,
                          "LockClassState must be standard layout!");
            uint8_t* byte_pointer = reinterpret_cast<uint8_t*>(this);
            uint8_t* state_pointer =
                byte_pointer - __offsetof(LockClassState, loop_node_);
            return reinterpret_cast<LockClassState*>(state_pointer);
        }

        // Performs a relaxed, weak compare exchange on the parent pointer of
        // this loop node. Due to the loops in FindSet() and UnionSet() this
        // may fail due to races, the result is not required and will be
        // retried based on other conditions.  Relaxed order is used because
        // neither caller publishes any other stores, nor depends on any other
        // loads.
        void CompareExchangeParent(LoopNode** expected, LoopNode* desired) {
            parent.compare_exchange_weak(expected, desired,
                                         fbl::memory_order_relaxed,
                                         fbl::memory_order_relaxed);
        }

        // Removes this node from whatever disjoint set it belongs to and
        // returns it to its own separate set.
        void Reset() {
            parent.store(this, fbl::memory_order_relaxed);
        }
    };

    // Loop detector node.
    LoopNode loop_node_;

    // Loop detection using Tarjan's strongly connected components algorithm to
    // efficiently identify loops and disjoint set structures to store and
    // update the sets of nodes involved in loops.
    // NOTE: The loop detector methods, except FindSet and UnionSets, must only
    // be called from the loop detector thread.
    struct LoopDetector {
        // The maximum index of the last loop detection run. Node index values
        // are compared with this value to determine whether to revisit the
        // node. Using a generation count permits running subsequent passes
        // without first clearing the state of every node.
        uint64_t generation{0};

        // Running counter marking the step at which a node has been visited or
        // revisited.
        uint64_t index{0};

        // The head of the stack of active nodes in a path traversal. The bottom
        // of the stack is marked with the sentinel value 1 instead of nullptr
        // to simplify determining whether a node is on the stack. Every node on
        // the stack has LoopNode::next != nulltpr.
        LoopNode* stack{reinterpret_cast<LoopNode*>(1)};

        // Performs a single traversal of the lock dependency graph and updates
        // the disjoint set structures with any detected loops.
        void DetectionPass() {
            // The next generation starts at the end of the previous. All nodes
            // with indices less than or equal to the generation are revisited.
            generation = index;

            for (auto& state : LockClassState::Iter()) {
                if (state.loop_node_.index <= generation)
                    Connect(&state.loop_node_);
            }
        }

        // Recursively traverses a node path and updates the disjoint set
        // structures when loops are detected.
        void Connect(LoopNode* node) {
            index += 1;
            node->index = index;
            node->least = index;
            Push(node);

            // Evaluate each node along the out edges of the dependency graph.
            const auto& out_edges = node->ToState()->dependency_set();
            for (LockClassId id : out_edges) {
                LoopNode* related_node = &Get(id)->loop_node_;
                if (related_node->index <= generation) {
                    Connect(related_node);
                    node->least = fbl::min(node->least, related_node->least);
                } else if (related_node->next != nullptr) {
                    node->least = fbl::min(node->least, related_node->index);
                }
            }

            // Update the disjoint set structures. Other nodes above this one on
            // the stack are merged into this set.
            if (node->index == node->least) {
                LoopNode* top = nullptr;
                size_t set_size = 0;
                while (top != node) {
                    top = Pop();
                    UnionSets(node, top);
                    set_size++;
                }

                // Report loops with more than two components. Basic inversions
                // with only two locks are reported by ThreadLockState::Acquire.
                if (set_size > 2) {
                    LoopNode* root = FindSet(node);
                    SystemCircularLockDependencyDetected(root->ToState());
                }
            }
        }

        // Pushes a node on the active nodes stack.
        void Push(LoopNode* node) {
            ZX_DEBUG_ASSERT(node->next == nullptr);
            node->next = stack;
            stack = node;
        }

        // Pops a node from the active nodes stack.
        LoopNode* Pop() {
            ZX_DEBUG_ASSERT(stack != reinterpret_cast<LoopNode*>(1));
            LoopNode* node = stack;
            stack = node->next;
            node->next = nullptr;
            return node;
        }

        // Finds the parent node of the disjoint set this node belongs to. This
        // approach applies thread-safe path splitting to flatten the set as it
        // traverses the path, using the two-try optimization suggested by
        // Jayanti and Tarjan.
        static LoopNode* FindSet(LoopNode* node) {
            while (true) {
                // First pass: either terminate or attempt path split.
                LoopNode* parent = node->parent.load(fbl::memory_order_relaxed);
                LoopNode* grandparent = parent->parent.load(fbl::memory_order_relaxed);
                if (parent == grandparent)
                    return parent;
                node->CompareExchangeParent(&parent, grandparent);

                // Second pass: either terminate, retry if last pass failed, or
                // advance and attempt path split.
                parent = node->parent.load(fbl::memory_order_relaxed);
                grandparent = parent->parent.load(fbl::memory_order_relaxed);
                if (parent == grandparent)
                    return parent;
                node->CompareExchangeParent(&parent, grandparent);

                // Advance regardless of whether split succeeded or failed.
                node = parent;
            }
        }

        // Joins the disjoint sets for the given nodes. Performs linking based
        // on address order, which approximates the randomized total order
        // suggested by Jayanti and Tarjan.
        static void UnionSets(LoopNode* a, LoopNode* b) {
            while (true) {
                LoopNode* root_a = FindSet(a);
                LoopNode* root_b = FindSet(b);

                a = root_a;
                b = root_b;

                if (root_a == root_b) {
                    return; // Nothing to do for nodes in the same set.
                } else if (root_a < root_b) {
                    root_b->CompareExchangeParent(&root_b, root_a);
                } else {
                    root_a->CompareExchangeParent(&root_a, root_b);
                }
            }
        }
    };
};

// Runs a loop detection pass to find circular lock dependencies. This must be
// invoked at some point in the future after the lock validator calls the
// system-defined SystemTriggerLoopDetection().
static inline void LoopDetectionPass() {
    LockClassState::LoopDetectionPass();
}

} // namespace lockdep
