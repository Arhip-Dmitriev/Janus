#ifndef JANUS_BACKEND_QISKIT_HPP
#define JANUS_BACKEND_QISKIT_HPP

#include "ir.hpp"
#include "error.hpp"

#include <cstdint>
#include <string>

namespace janus {

// QiskitBackend :: inputs the Janus IR and emits a complete standalone
// Python file with all necessary Qiskit imports already included.
//
// The emitted Python mirrors the control flow of the Janus source exactly.
// All gates map to their Qiskit equivalents via qiskit.circuit.library
// gate classes wrapped in qiskit.quantum_info.Operator.  Quantum states
// are represented as qiskit.quantum_info.Statevector objects.  Classical
// control flow is emitted as equivalent Python if/for/while statements.
//
// The output file name is derived from the source file name by replacing
// the .jan extension with .py, unless overridden by the -o flag.

class QiskitBackend {
public:
    // Emits the complete Python file for the given IR program.
    // output_path is the full path to the .py file to write.
    // On any I/O failure, report_error is called.
    void emit(const IRProgram& program, const std::string& output_path);
};

} // namespace janus

#endif // JANUS_BACKEND_QISKIT_HPP
