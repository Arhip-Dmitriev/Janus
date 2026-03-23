#include "scope.hpp"

namespace janus {


// Constructor: creates the single script-level outer scope (frame 0).

Scope::Scope() {
    frames_.emplace_back();
}


// Block scope management.


void Scope::push_block() {
    frames_.emplace_back();
}

void Scope::pop_block() {
    // Frame 0 (script-level) must never be popped.
    if (frames_.size() <= 1) {
        report_error(get_error_line());
    }

    // Quantum registers held by variables in this frame are silently
    // discarded.  No measurement and no reset.  The unique_ptrs inside
    // JanusValue::quantum_val are destroyed automatically when the
    // unordered_map entries are destroyed, which releases the
    // QuantumState memory without any side effects.
    frames_.pop_back();
}


// Function scope management.


void Scope::push_function() {
    ScopeFrame frame;
    frame.is_function_boundary = true;
    frames_.push_back(std::move(frame));
}

void Scope::pop_function() {
    if (frames_.size() <= 1) {
        report_error(get_error_line());
    }
    // Same silent-discard semantics as pop_block for quantum registers.
    frames_.pop_back();
}


// Assignment.

// Janus block-scoping model: every assignment targets the innermost
// (current) scope frame.  If the name already exists in that frame, the
// value is updated with a type compatibility check.  If the name does NOT
// exist in the innermost frame, a new variable is created there,
// regardless of whether the name exists in any outer frame (this is
// shadowing).

void Scope::assign(const std::string& name, JanusValue value,
                   uint32_t line) {
    auto& current = frames_.back().variables;
    auto it = current.find(name);

    if (it != current.end()) {
        // Variable already exists in the current frame.
        // Re-assignment type check: the new value must be compatible with
        // the existing variable's type.
        JanusType existing_type = it->second.type_info.type;
        JanusType new_type      = value.type_info.type;

        if (!is_assignable_without_cast(existing_type, new_type)) {
            report_error(line);
        }

        it->second = std::move(value);
    } else {
        // Name does not exist in the current frame.  Create a new variable
        // (potentially shadowing an outer-scope variable of the same name).
        current.emplace(name, std::move(value));
    }
}


// Loop variable persistence.

// For for-loop init variables and foreach element variables: the variable
// is placed directly in the current (enclosing) scope frame so that it
// persists after the loop body scope is popped.

// Collision rule: if a variable of the same name already exists in the
// current frame, that is an error.  This prevents the loop variable from
// silently overwriting a same-level variable.

void Scope::declare_loop_variable(const std::string& name, JanusValue value,
                                  uint32_t line) {
    auto& current = frames_.back().variables;

    if (current.find(name) != current.end()) {
        // A variable with this name already exists at the same scope level
        // where the loop variable would persist.  This is an error per the
        // documentation note on loop variable and outer scope collision.
        report_error(line);
    }

    current.emplace(name, std::move(value));
}


// Loop variable update.

// After the loop variable has been declared in the enclosing scope via
// declare_loop_variable, the executor updates it on each iteration (for
// the foreach element assignment or for the for-loop update expression).
// This searches from innermost outward to find the variable and updates
// it in place.

void Scope::update_loop_variable(const std::string& name, JanusValue value,
                                 uint32_t line) {
    // Walk from innermost to outermost, respecting function boundaries.
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        auto var_it = it->variables.find(name);
        if (var_it != it->variables.end()) {
            // Type check on update: the loop variable's type was set at
            // declaration; subsequent values must be compatible.
            JanusType existing_type = var_it->second.type_info.type;
            JanusType new_type      = value.type_info.type;

            if (!is_assignable_without_cast(existing_type, new_type)) {
                report_error(line);
            }

            var_it->second = std::move(value);
            return;
        }

        // Stop at function boundaries.
        if (it->is_function_boundary) {
            break;
        }
    }

    // Should not reach here if declare_loop_variable was called first.
    report_error(line);
}


// Lookup
// Searches from the innermost scope frame outward.  Stops at function
// boundaries to enforce function isolation.

JanusValue* Scope::lookup(const std::string& name) {
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        auto var_it = it->variables.find(name);
        if (var_it != it->variables.end()) {
            return &var_it->second;
        }

        // Function boundaries block further lookup.  The function's own
        // frame has already been searched; do not continue to enclosing
        // or global scopes.
        if (it->is_function_boundary) {
            return nullptr;
        }
    }

    return nullptr;
}

const JanusValue* Scope::lookup(const std::string& name) const {
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        auto var_it = it->variables.find(name);
        if (var_it != it->variables.end()) {
            return &var_it->second;
        }

        if (it->is_function_boundary) {
            return nullptr;
        }
    }

    return nullptr;
}


// Existence check


bool Scope::exists(const std::string& name) const {
    return lookup(name) != nullptr;
}

bool Scope::exists_in_current_frame(const std::string& name) const {
    return frames_.back().variables.find(name) !=
           frames_.back().variables.end();
}


// Depth and state


uint32_t Scope::depth() const noexcept {
    // Frame 0 is depth 0 (script-level).
    return static_cast<uint32_t>(frames_.size()) - 1;
}

bool Scope::in_function() const noexcept {
    return frames_.back().is_function_boundary;
}

} // namespace janus
