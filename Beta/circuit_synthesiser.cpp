#include "circuit_synthesiser.hpp"

#include <complex>
#include <cstdint>
#include <vector>

namespace janus {
namespace circuit_synth {


// Internal helpers


// Returns the number of qubits implied by a state-vector dimension.
// dim must be a power of 2 and at least 2.  Returns 0 on failure.
static uint32_t qubits_from_dim(uint64_t dim) {
    if (dim == 0 || (dim & (dim - 1)) != 0) {
        return 0;
    }
    uint32_t q = 0;
    uint64_t d = dim;
    while (d > 1) {
        d >>= 1;
        ++q;
    }
    return q;
}


// Compute the total number of qubit lines implied by a vector of
// QuantumStates.  Each QuantumState contributes its own num_qubits().
static uint32_t total_qubits(const std::vector<QuantumState>& states) {
    uint32_t total = 0;
    for (const auto& s : states) {
        total += s.num_qubits();
    }
    return total;
}


// Construction helpers


std::vector<QuantumState> parse_qubit_arg(const JanusValue& arg,
                                          uint32_t line) {
    std::vector<QuantumState> result;

    switch (arg.type_info.type) {

        case JanusType::CNUM: {
            // Integer value gives the number of |0> qubits.
            int64_t n = static_cast<int64_t>(arg.real_val);
            if (n < 1) {
                report_error(line);
            }
            result.reserve(static_cast<std::size_t>(n));
            for (int64_t i = 0; i < n; ++i) {
                result.emplace_back();  // default: 1-qubit |0>
            }
            break;
        }

        case JanusType::CBIT: {
            // Treats cbit value as a qubit count (0 or 1).
            // A cbit of 0 means 0 qubits which is degenerate; treat 0
            // as requesting a single qubit in |0> and 1 as a single
            // qubit in |1>.
            uint64_t basis = (arg.real_val != 0.0) ? 1 : 0;
            result.emplace_back(1, basis, line);
            break;
        }

        case JanusType::QUBIT: {
            if (!arg.quantum_val) {
                report_error(line);
            }
            result.push_back(*arg.quantum_val);
            break;
        }

        case JanusType::QNUM: {
            if (!arg.quantum_val) {
                report_error(line);
            }
            // A qnum contributes its full multi-qubit QuantumState.
            result.push_back(*arg.quantum_val);
            break;
        }

        case JanusType::LIST: {
            if (!arg.list_data) {
                report_error(line);
            }
            const auto& elems = *arg.list_data;
            if (elems.empty()) {
                report_error(line);
            }
            result.reserve(elems.size());
            for (const auto& elem : elems) {
                if (elem.type_info.type == JanusType::QUBIT) {
                    if (!elem.quantum_val) {
                        report_error(line);
                    }
                    result.push_back(*elem.quantum_val);
                } else if (elem.type_info.type == JanusType::QNUM) {
                    if (!elem.quantum_val) {
                        report_error(line);
                    }
                    result.push_back(*elem.quantum_val);
                } else if (elem.type_info.type == JanusType::NULL_TYPE) {
                    // Null in a qubit list is treated as a |0> qubit.
                    result.emplace_back();
                } else {
                    // Non-quantum element in qubit list is an error.
                    report_error(line);
                }
            }
            break;
        }

        case JanusType::MATRIX: {
            // Interpret as a column state vector.
            if (!arg.matrix_data) {
                report_error(line);
            }
            const auto& data = *arg.matrix_data;
            uint64_t dim = data.size();
            uint32_t nq = qubits_from_dim(dim);
            if (nq == 0) {
                report_error(line);
            }
            // Build a QuantumState from the raw amplitudes.
            QuantumState qs(nq);
            for (uint64_t i = 0; i < dim; ++i) {
                qs.amplitude(i) = data[i];
            }
            // Normalise automatically with no warning
            qs.normalise(line);
            result.push_back(std::move(qs));
            break;
        }

        case JanusType::CSTR: {
            // Dirac-notation string.
            QuantumState qs = QuantumState::from_dirac_string(
                arg.str_val, line);
            result.push_back(std::move(qs));
            break;
        }

        case JanusType::NULL_TYPE: {
            // Null first argument: zero qubits.  This is degenerate but
            // not an error.
            break;
        }

        default:
            // GATE, CIRC, BLOCK, FUNCTION, and other types are not valid
            // as the qubit argument to circ().
            report_error(line);
    }

    return result;
}


std::vector<std::vector<JanusValue>> parse_gate_grid(const JanusValue& arg,
                                                     uint32_t line) {
    std::vector<std::vector<JanusValue>> grid;

    switch (arg.type_info.type) {

        case JanusType::LIST: {
            if (!arg.list_data) {
                report_error(line);
            }
            const auto& outer = *arg.list_data;
            if (outer.empty()) {
                // Empty gate grid: zero qubit lines, zero time steps.
                return grid;
            }

            // Each element of the outer list must be a LIST (row of gates).
            grid.reserve(outer.size());
            uint32_t expected_cols = 0;

            for (std::size_t r = 0; r < outer.size(); ++r) {
                const auto& row_val = outer[r];
                if (row_val.type_info.type != JanusType::LIST) {
                    report_error(line);
                }
                if (!row_val.list_data) {
                    report_error(line);
                }
                const auto& row_elems = *row_val.list_data;

                if (r == 0) {
                    expected_cols = static_cast<uint32_t>(row_elems.size());
                } else if (static_cast<uint32_t>(row_elems.size()) !=
                           expected_cols) {
                    // All rows must have the same number of columns.
                    report_error(line);
                }

                std::vector<JanusValue> row;
                row.reserve(row_elems.size());
                for (const auto& elem : row_elems) {
                    if (elem.type_info.type != JanusType::GATE &&
                        elem.type_info.type != JanusType::NULL_TYPE) {
                        report_error(line);
                    }
                    row.push_back(elem);
                }
                grid.push_back(std::move(row));
            }
            break;
        }

        case JanusType::BLOCK: {
            if (!arg.block_data) {
                report_error(line);
            }
            // Deep-copy the block's gate grid.
            grid = arg.block_data->gate_grid;
            break;
        }

        case JanusType::MATRIX: {
            // A numeric matrix cannot hold gate values.  This path would
            // only be reached if the matrix somehow contained gate-typed
            // data, which is not possible in the Beta type system.
            report_error(line);
            break;
        }

        case JanusType::NULL_TYPE: {
            // Null gate argument: empty gate grid.
            break;
        }

        default:
            report_error(line);
    }

    return grid;
}


void validate_gate_grid(const std::vector<std::vector<JanusValue>>& grid,
                        uint32_t num_qubits,
                        uint32_t line) {
    if (static_cast<uint32_t>(grid.size()) != num_qubits) {
        report_error(line);
    }

    if (num_qubits == 0) {
        return;
    }

    // All rows must have the same length.
    uint32_t time_steps = static_cast<uint32_t>(grid[0].size());
    for (uint32_t i = 1; i < num_qubits; ++i) {
        if (static_cast<uint32_t>(grid[i].size()) != time_steps) {
            report_error(line);
        }
    }

    // Validate each entry and check multi-qubit gate spans.
    for (uint32_t t = 0; t < time_steps; ++t) {
        uint32_t row = 0;
        while (row < num_qubits) {
            const JanusValue& entry = grid[row][t];

            if (entry.type_info.type == JanusType::NULL_TYPE) {
                // Null: identity placeholder.  No span to check.
                ++row;
                continue;
            }

            if (entry.type_info.type != JanusType::GATE) {
                report_error(line);
            }

            uint32_t gate_width = entry.type_info.width;
            if (gate_width == 0) {
                // A gate with unknown width is invalid in a grid.
                report_error(line);
            }

            // The gate spans rows [row, row + gate_width - 1].
            if (row + gate_width > num_qubits) {
                // Gate exceeds available qubit lines.
                report_error(line);
            }

            // All rows spanned by this gate (except the first) must be
            // null at this time step.
            for (uint32_t k = 1; k < gate_width; ++k) {
                if (grid[row + k][t].type_info.type != JanusType::NULL_TYPE) {
                    report_error(line);
                }
            }

            // Advance past the spanned rows.
            row += gate_width;
        }
    }
}


JanusValue build_circ(std::vector<QuantumState> qubit_states,
                      std::vector<std::vector<JanusValue>> gate_grid,
                      uint32_t line) {
    // Determine the number of qubit lines.  This is the number of rows
    // in the gate grid.  If the gate grid is empty, use the total qubit
    // count from the states.
    uint32_t num_lines;
    if (!gate_grid.empty()) {
        num_lines = static_cast<uint32_t>(gate_grid.size());
    } else {
        num_lines = total_qubits(qubit_states);
    }

    // When the qubit states vector contains a single multi-qubit state
    // whose width matches the number of grid lines, it represents the
    // combined initial state.  No further decomposition is needed; the
    // backend will use it directly.
    // When the qubit states vector contains multiple entries (one per
    // qubit line, possibly each being 1-qubit), verify the total qubit
    // count matches the number of grid lines.
    uint32_t tq = total_qubits(qubit_states);
    if (tq != num_lines && num_lines != 0) {
        report_error(line);
    }

    validate_gate_grid(gate_grid, num_lines, line);

    return JanusValue::make_circ(std::move(qubit_states),
                                 std::move(gate_grid));
}


JanusValue build_block(std::vector<std::vector<JanusValue>> gate_grid,
                       uint32_t line) {
    uint32_t num_lines = static_cast<uint32_t>(gate_grid.size());
    validate_gate_grid(gate_grid, num_lines, line);
    return JanusValue::make_block(std::move(gate_grid));
}


// Gate application


void apply_gate_to_state(QuantumState& state,
                         const JanusValue& gate,
                         const std::vector<uint32_t>& targets,
                         uint32_t line) {
    // Null gates are identity: nothing to do.
    if (gate.type_info.type == JanusType::NULL_TYPE) {
        return;
    }

    if (gate.type_info.type != JanusType::GATE) {
        report_error(line);
    }

    if (!gate.matrix_data) {
        report_error(line);
    }

    uint32_t gate_width = gate.type_info.width;
    uint32_t num_targets = static_cast<uint32_t>(targets.size());
    uint32_t N = state.num_qubits();

    if (gate_width != num_targets) {
        report_error(line);
    }

    if (gate_width > N) {
        report_error(line);
    }

    // Validate target indices are in range and unique.
    for (uint32_t i = 0; i < num_targets; ++i) {
        if (targets[i] >= N) {
            report_error(line);
        }
        for (uint32_t j = i + 1; j < num_targets; ++j) {
            if (targets[i] == targets[j]) {
                report_error(line);
            }
        }
    }

    uint32_t dim = 1u << gate_width;
    const auto& U = *gate.matrix_data;

    if (U.size() != static_cast<uint64_t>(dim) * dim) {
        report_error(line);
    }

    auto& amps = state.amplitudes();
    uint64_t total_amps = state.num_amplitudes();

    // Bit positions within the full state index for each target qubit.
    // Qubit q maps to bit position (N - 1 - q) in the amplitude index,
    // following the standard convention where qubit 0 is the
    // most-significant bit.
    std::vector<uint32_t> bit_pos(gate_width);
    for (uint32_t i = 0; i < gate_width; ++i) {
        bit_pos[i] = N - 1 - targets[i];
    }

    // Bitmask covering all target qubit bit positions.
    uint64_t target_mask = 0;
    for (uint32_t i = 0; i < gate_width; ++i) {
        target_mask |= (uint64_t{1} << bit_pos[i]);
    }

    // Scratch buffers for the 2^W sub-vector and the result.
    std::vector<std::complex<double>> sub(dim);
    std::vector<std::complex<double>> result(dim);

    // Iterate over all basis state indices.  Process only "base" indices
    // where all target qubit bits are zero.  For each base, extract the
    // 2^W amplitudes, multiply by U, and write back.
    for (uint64_t base = 0; base < total_amps; ++base) {
        // Skip if any target bit is set: this index is part of a group
        // whose base has those bits cleared.
        if (base & target_mask) {
            continue;
        }

        // Extract the sub-vector.
        for (uint32_t j = 0; j < dim; ++j) {
            uint64_t idx = base;
            // For each bit k in j, set the corresponding target qubit
            // bit in idx.  Bit (gate_width - 1 - k) of j corresponds to
            // target qubit k (gate qubit 0 is the MSB of j).
            for (uint32_t k = 0; k < gate_width; ++k) {
                if (j & (1u << (gate_width - 1 - k))) {
                    idx |= (uint64_t{1} << bit_pos[k]);
                }
            }
            sub[j] = amps[idx];
        }

        // Matrix-vector multiply: result = U * sub.
        for (uint32_t r = 0; r < dim; ++r) {
            std::complex<double> acc{0.0, 0.0};
            for (uint32_t c = 0; c < dim; ++c) {
                acc += U[r * dim + c] * sub[c];
            }
            result[r] = acc;
        }

        // Write the result back into the state amplitudes.
        for (uint32_t j = 0; j < dim; ++j) {
            uint64_t idx = base;
            for (uint32_t k = 0; k < gate_width; ++k) {
                if (j & (1u << (gate_width - 1 - k))) {
                    idx |= (uint64_t{1} << bit_pos[k]);
                }
            }
            amps[idx] = result[j];
        }
    }
}


// Circuit inspection


uint32_t count_gates(
        const std::vector<std::vector<JanusValue>>& grid) {
    uint32_t count = 0;
    for (const auto& row : grid) {
        for (const auto& entry : row) {
            if (entry.type_info.type != JanusType::NULL_TYPE) {
                ++count;
            }
        }
    }
    return count;
}


uint32_t circuit_depth(
        const std::vector<std::vector<JanusValue>>& grid) {
    if (grid.empty()) {
        return 0;
    }
    return static_cast<uint32_t>(grid[0].size());
}


uint32_t qubit_count(
        const std::vector<std::vector<JanusValue>>& grid) {
    return static_cast<uint32_t>(grid.size());
}


std::vector<JanusValue> extract_gates(
        const std::vector<std::vector<JanusValue>>& grid) {
    std::vector<JanusValue> gates;
    for (const auto& row : grid) {
        for (const auto& entry : row) {
            if (entry.type_info.type != JanusType::NULL_TYPE) {
                gates.push_back(entry);
            }
        }
    }
    return gates;
}


} // namespace circuit_synth
} // namespace janus
