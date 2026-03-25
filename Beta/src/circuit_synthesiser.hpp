#ifndef JANUS_CIRCUIT_SYNTHESISER_HPP
#define JANUS_CIRCUIT_SYNTHESISER_HPP

#include "value.hpp"
#include "quantum_state.hpp"
#include "error.hpp"

#include <cstdint>
#include <vector>

namespace janus {
namespace circuit_synth {


// Construction helpers


// Parse the first argument to circ() into a vector of qubit initial states.
// Accepted argument types:
//   CNUM:   integer value N produces N qubits each in the |0> state.
//           A value less than 1 is an error.
//   QUBIT:  a single qubit; returns a vector of one QuantumState.
//   QNUM:   a multi-qubit register; returns a vector containing the
//           single multi-qubit QuantumState (the backend will use it
//           as the combined initial state for the circuit).
//   LIST:   each element must be QUBIT or QNUM.  Each contributes its
//           QuantumState to the output.  A QUBIT contributes a 1-qubit
//           state; a QNUM contributes its full multi-qubit state.
//   MATRIX: interpreted as a column state vector.  The number of rows
//           must be a power of 2.  Returns a vector containing a single
//           QuantumState with num_qubits = log2(rows).
//   CSTR:   Dirac-notation string.  Returns a vector containing a single
//           QuantumState parsed from the string.
//   NULL_TYPE: treated as 0 qubits; produces an empty vector.
// Any other type is an error.
std::vector<QuantumState> parse_qubit_arg(const JanusValue& arg,
                                          uint32_t line);


// Parse the second argument to circ() or the argument to block() into
// a gate grid: gate_grid[qubit_line][time_step].
// Accepted argument types:
//   LIST:   interpreted as a list of lists.  Each inner element must be
//           a LIST whose elements are GATE or NULL_TYPE values.  All
//           inner lists must have the same length.
//   BLOCK:  the block's existing gate_grid is deep-copied.
//   MATRIX: rows and columns are used to extract gates from the matrix
//           data.  This path is valid only when the matrix literal was
//           evaluated with gate-typed elements stored in a list-of-lists
//           representation.  In practice, a matrix of gates will have
//           been evaluated to a LIST of LISTs by the backend; this case
//           is provided for robustness.
// Any other type is an error.
std::vector<std::vector<JanusValue>> parse_gate_grid(const JanusValue& arg,
                                                     uint32_t line);


// Validate a gate grid for structural correctness.
//   - All rows must have the same length (number of time steps).
//   - Every entry must be of type GATE or NULL_TYPE.
//   - A multi-qubit gate of width W at row i, column t requires that
//     rows i+1 through i+W-1 at column t are NULL_TYPE; otherwise the
//     gate would exceed the available qubit lines or overlap with
//     another gate.
//   - num_qubits must equal the number of rows.
// On any violation, report_error(line) is called.
void validate_gate_grid(const std::vector<std::vector<JanusValue>>& grid,
                        uint32_t num_qubits,
                        uint32_t line);


// Construct a fully validated CIRC JanusValue from qubit states and a
// gate grid.  Calls validate_gate_grid internally.
JanusValue build_circ(std::vector<QuantumState> qubit_states,
                      std::vector<std::vector<JanusValue>> gate_grid,
                      uint32_t line);


// Construct a fully validated BLOCK JanusValue from a gate grid.
// Calls validate_gate_grid internally.
JanusValue build_block(std::vector<std::vector<JanusValue>> gate_grid,
                       uint32_t line);


// Gate application


// Apply a single gate's unitary matrix to a QuantumState at the specified
// target qubit indices within that state.

// gate must be of type GATE with a valid matrix_data pointer.
// targets lists the qubit indices within 'state' that the gate acts on,
// in order matching the gate's qubit ordering: targets[0] is the
// gate's qubit 0 (most-significant in the gate matrix indexing),
// targets[1] is the gate's qubit 1, and so forth.

// The gate's qubit width must equal targets.size().  Each target index
// must be less than state.num_qubits().  Duplicate target indices are
// an error.

// Null gates (NULL_TYPE) are silently skipped as identity operations.
void apply_gate_to_state(QuantumState& state,
                         const JanusValue& gate,
                         const std::vector<uint32_t>& targets,
                         uint32_t line);


// Circuit inspection


// Count the number of non-null gate entries in a gate grid.
// Null entries (identity placeholders) are never counted.  A multi-qubit
// gate is counted once at the row where it appears; the null entries in
// the rows it spans do not add to the count.
uint32_t count_gates(
    const std::vector<std::vector<JanusValue>>& grid);


// Return the circuit depth (number of time steps) of a gate grid.
// Returns 0 for an empty grid.
uint32_t circuit_depth(
    const std::vector<std::vector<JanusValue>>& grid);


// Return the number of qubit lines in a gate grid.
uint32_t qubit_count(
    const std::vector<std::vector<JanusValue>>& grid);


// Extract all non-null gates from a gate grid in row-major order
// (row 0 left-to-right, then row 1, ...).  Multi-qubit gates appear
// once at the row where they are placed.
std::vector<JanusValue> extract_gates(
    const std::vector<std::vector<JanusValue>>& grid);


} // namespace circuit_synth
} // namespace janus

#endif // JANUS_CIRCUIT_SYNTHESISER_HPP
