#include "error.hpp"

#include <cstdlib>
#include <iostream>

namespace janus {

// Module-level line number tracker.  Every compiler phase updates this
// value via set_error_line() so that the line printed in the error message
// corresponds to the source location being processed when the error occurs.
static uint32_t current_line = 0;

void set_error_line(uint32_t line) noexcept {
    current_line = line;
}

uint32_t get_error_line() noexcept {
    return current_line;
}

void report_error() {
    std::cerr << "Error (line " << current_line << "): unknown error"
              << std::endl;
    std::exit(1);
}

void report_error(uint32_t line) {
    set_error_line(line);
    report_error();
}

} // namespace janus
