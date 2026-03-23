#ifndef JANUS_QUANTUM_STATE_HPP
#define JANUS_QUANTUM_STATE_HPP

#include "types.hpp"

#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace janus {

// QuantumState :: internal amplitude-level state vector representation
//
// Every qubit and qnum value in the Janus runtime is backed by a
// QuantumState.  A qubit is a 1-qubit QuantumState; a qnum is an
// N-qubit QuantumState whose width is determined dynamically from the
// initialising value or from the Dirac-notation string.
//
// Amplitudes are stored as std::complex<double> (64-bit real + 64-bit
// imaginary).  The vector length is always 2^num_qubits.  Basis states
// are indexed in standard binary computational-basis order: index 0 is
// |00...0>, index 1 is |00...1>, and so on.
//
// Assignment is by value (deep copy) consistent with Janus semantics.

class QuantumState {
public:
    // Default: 1-qubit register in the |0> state.
    QuantumState();

    // N-qubit register in the |0...0> state.
    explicit QuantumState(uint32_t num_qubits);

    // N-qubit register in the |basis_state> computational basis state.
    // If basis_state >= 2^num_qubits, report_error is called.
    QuantumState(uint32_t num_qubits, uint64_t basis_state, uint32_t line);

    // Construct from a Dirac-notation string.
    // Amplitude coefficients are compile-time expressions that may contain
    // numeric literals, sqrt/sin/cos, pi/e, complex i, negative values,
    // and arithmetic (+, -, *, /, ^).  Variable references are not
    // permitted and produce an error.  The result is automatically
    // normalised with no warning.  A malformed string is an error.
    static QuantumState from_dirac_string(const std::string& dirac,
                                          uint32_t line);

    // Accessors
    uint32_t num_qubits() const noexcept;
    uint64_t num_amplitudes() const noexcept;

    const std::complex<double>& amplitude(uint64_t index) const noexcept;
    std::complex<double>&       amplitude(uint64_t index) noexcept;

    const std::vector<std::complex<double>>& amplitudes() const noexcept;
    std::vector<std::complex<double>>&       amplitudes() noexcept;

    // Squared norm (sum of |a_i|^2).
    double norm_sq() const noexcept;

    // Normalise in place.  If the norm is zero, report_error is called.
    void normalise(uint32_t line);

    // Element-wise arithmetic operations.
    // Both operands are zero-padded to the larger dimension if sizes
    // differ.  The result is automatically renormalised.  Division or
    // modulus by a zero amplitude is an error.
    QuantumState add(const QuantumState& rhs, uint32_t line) const;
    QuantumState subtract(const QuantumState& rhs, uint32_t line) const;
    QuantumState multiply(const QuantumState& rhs, uint32_t line) const;
    QuantumState divide(const QuantumState& rhs, uint32_t line) const;
    QuantumState modulus(const QuantumState& rhs, uint32_t line) const;
    QuantumState power(const QuantumState& rhs, uint32_t line) const;

    // Measurement: probabilistically collapses the state to a single
    // basis state and returns the index of that state (the classical
    // numeric value).  After measurement, the state vector contains
    // amplitude 1.0 at the collapsed index and 0.0 elsewhere.
    uint64_t measure(uint32_t line);

    // Peek: performs the same probabilistic selection as measure but
    // does NOT collapse the state.  Returns the basis-state index.
    uint64_t peek(uint32_t line) const;

    // Format the state as a Dirac-notation string suitable for printing.
    // Global phase is normalised out, near-zero amplitudes are suppressed,
    // and amplitudes are printed at maximum double precision.
    std::string to_dirac_string() const;

private:
    std::vector<std::complex<double>> amplitudes_;
    uint32_t num_qubits_;

    // Helper: resize the shorter of two states to match the larger,
    // returning the unified qubit count and padded copies.
    static void unify_dimensions(const QuantumState& a,
                                 const QuantumState& b,
                                 std::vector<std::complex<double>>& out_a,
                                 std::vector<std::complex<double>>& out_b,
                                 uint32_t& out_qubits);

    // Helper: select a basis-state index by sampling the probability
    // distribution defined by the amplitudes.
    uint64_t sample_outcome() const;
};

} // namespace janus

#endif // JANUS_QUANTUM_STATE_HPP
