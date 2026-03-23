#ifndef JANUS_ERROR_HPP
#define JANUS_ERROR_HPP

#include <cstdint>
#include <string_view>

namespace janus {

// Beta error system.
//
// One error code.  When any error condition is
// detected during compilation or execution the program prints
//     Error (line N): unknown error
// to stderr and halts immediately via std::exit(1).
//
// There is no try/catch/throw syntax in Beta and no recovery mechanism.
// The error module exposes a single reporting function and a global
// line-number tracker that every phase of the compiler updates as it
// processes source.

// Sets the current line number tracked by the error system.
// Every phase (lexer, parser, type checker, IR generator, executor) must
// call this as it advances through source or IR nodes so that the line
// reported in any error message is accurate.
void set_error_line(uint32_t line) noexcept;

// Returns the current line number tracked by the error system.
uint32_t get_error_line() noexcept;

// Reports the single Beta error and terminates the process.
// Prints "Error (line N): unknown error" to stderr where N is the
// current line number, then calls std::exit(1).
// This function never returns.
[[noreturn]] void report_error();

// Convenience overload that sets the line number before reporting.
// Equivalent to set_error_line(line) followed by report_error().
// This function never returns.
[[noreturn]] void report_error(uint32_t line);

} // namespace janus

#endif // JANUS_ERROR_HPP
