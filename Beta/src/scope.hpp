#ifndef JANUS_SCOPE_HPP
#define JANUS_SCOPE_HPP

#include "value.hpp"
#include "types.hpp"
#include "error.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace janus {

// ScopeFrame :: a single level of variable bindings.
// Each block ({ ... }) in Janus creates a new ScopeFrame.  Function scopes
// set is_function_boundary to true, which prevents variable lookup from
// crossing into any enclosing scope.

struct ScopeFrame {
    std::unordered_map<std::string, JanusValue> variables;

    // When true, variable lookup stops at this frame and will not search
    // any outer frames.  Set for function body scopes to enforce full
    // isolation.
    bool is_function_boundary = false;
};


// Scope :: runtime variable environment for the Janus Beta interpreter.
// Implements:
//   - Single flat script-level outer scope (frame index 0).
//   - Block-level scoping where every assignment in a block creates a new
//     variable in that block (shadowing any outer variable of the same
//     name without modifying it).
//   - Function isolation via boundary frames that prevent any lookup from
//     reaching enclosing or global scopes.
//   - Loop variable persistence for for and foreach loops, where the
//     iteration/initialisation variable is placed directly in the
//     enclosing scope rather than in the loop body scope.
//   - Collision detection: a loop variable that would persist into a scope
//     that already contains a variable of the same name at that frame is
//     an error.
//   - Re-assignment type checking: updating a variable already in the
//     current frame requires type compatibility (same JanusType, or the
//     source is NULL_TYPE, or the assignment is allowed without cast per
//     is_assignable_without_cast).
//   - Silent discard of quantum registers when a scope frame is popped.

class Scope {
public:
    // Constructs a Scope with a single script-level frame (frame 0).
    Scope();

    // Pushes a new block scope.  Variables declared inside will be
    // destroyed when pop_block() is called.
    void push_block();

    // Pops the innermost block scope.  Any quantum registers held by
    // variables in this frame are silently discarded (their unique_ptrs
    // are simply destroyed).  Must not pop frame 0.
    void pop_block();

    // Pushes an isolated function scope.  Lookup will not cross this
    // boundary into any enclosing or global scope.
    void push_function();

    // Pops a function scope.  Equivalent to pop_block() but asserts that
    // the popped frame was a function boundary.
    void pop_function();

    // Assigns a value to a variable.
    // Semantics (matching the Janus block-scoping model):
    //   1. If the name exists in the innermost (current) scope frame,
    //      the value is updated in place after a type compatibility check.
    //      Re-assigning to a different JanusType is an error unless
    //      is_assignable_without_cast allows it.
    //   2. If the name does NOT exist in the innermost frame, a new
    //      variable is created in the innermost frame.  This shadows any
    //      outer variable of the same name.

    // line is used for error reporting.
    void assign(const std::string& name, JanusValue value, uint32_t line);

    // Declares a loop variable (for init or foreach element) in the
    // CURRENT (enclosing) scope frame, which is the frame where the loop
    // statement itself lives.

    // Before creating the variable, checks for a collision: if a variable
    // of the same name already exists in the current frame, that is an
    // error.  Variables in outer frames are not checked (the loop variable
    // would shadow them, but since it persists at this level, a same-level
    // collision is the only error condition).

    // The variable is created in the current frame so that it naturally
    // persists after the loop body scope is popped.
    void declare_loop_variable(const std::string& name, JanusValue value,
                               uint32_t line);

    // Updates a loop variable that was previously created via
    // declare_loop_variable.  Searches from the innermost frame outward
    // (respecting function boundaries) and updates the first match.
    // If not found, reports an error (should not happen if the executor
    // calls declare_loop_variable before the loop body).
    void update_loop_variable(const std::string& name, JanusValue value,
                              uint32_t line);

    // Looks up a variable by name.  Searches from the innermost frame
    // outward, stopping at any function boundary.  Returns a pointer to
    // the JanusValue if found, or nullptr if the variable does not exist
    // in any reachable scope.
    JanusValue* lookup(const std::string& name);
    const JanusValue* lookup(const std::string& name) const;

    // Returns true if the given name is resolvable via lookup.
    bool exists(const std::string& name) const;

    // Returns true if the given name exists specifically in the current
    // (innermost) scope frame.
    bool exists_in_current_frame(const std::string& name) const;

    // Returns the current nesting depth (0 = script-level).
    uint32_t depth() const noexcept;

    // Returns true if the current innermost frame is a function boundary.
    bool in_function() const noexcept;

private:
    std::vector<ScopeFrame> frames_;
};

} // namespace janus

#endif // JANUS_SCOPE_HPP
