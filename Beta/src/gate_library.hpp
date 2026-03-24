#ifndef JANUS_GATE_LIBRARY_HPP
#define JANUS_GATE_LIBRARY_HPP

#include "value.hpp"
#include "error.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace janus {
namespace gate_library {

using cx = std::complex<double>;

// Unitarity check: verifies U†U = I within double precision.
// Tolerance per element scales with dimension to account for
// floating-point accumulation in the inner products.
inline bool is_unitary(const std::vector<cx>& data, uint32_t dim) {
    double tol = static_cast<double>(dim) *
                 std::numeric_limits<double>::epsilon() * 4.0;
    for (uint32_t i = 0; i < dim; ++i) {
        for (uint32_t j = 0; j < dim; ++j) {
            cx dot{0.0, 0.0};
            for (uint32_t k = 0; k < dim; ++k) {
                // U†[i][k] = conj(U[k][i])
                dot += std::conj(data[k * dim + i]) * data[k * dim + j];
            }
            double expected = (i == j) ? 1.0 : 0.0;
            if (std::abs(dot.real() - expected) > tol ||
                std::abs(dot.imag()) > tol) {
                return false;
            }
        }
    }
    return true;
}

// Validates a gate matrix: must be square, 2^n x 2^n, and unitary.
inline void validate_gate_matrix(const std::vector<cx>& data,
                                 uint32_t dim, uint32_t line) {
    if (dim == 0) {
        report_error(line);
    }
    if (data.size() != static_cast<uint64_t>(dim) * dim) {
        report_error(line);
    }
    // Check dim is a power of 2.
    if ((dim & (dim - 1)) != 0) {
        report_error(line);
    }
    if (!is_unitary(data, dim)) {
        report_error(line);
    }
}

// Computes qubit width from matrix dimension.
inline uint32_t qubit_width_from_dim(uint32_t dim) {
    uint32_t w = 0;
    uint32_t d = dim;
    while (d > 1) {
        d >>= 1;
        ++w;
    }
    return w;
}

// Builds a JanusValue gate from a raw matrix, with validation.
inline JanusValue make_validated_gate(std::vector<cx> data,
                                      uint32_t dim, uint32_t line) {
    validate_gate_matrix(data, dim, line);
    uint32_t w = qubit_width_from_dim(dim);
    return JanusValue::make_gate(w, std::move(data));
}


// 1-qubit non-parameterised gates.


inline JanusValue gate_i(uint32_t line) {
    return make_validated_gate({
        {1,0}, {0,0},
        {0,0}, {1,0}
    }, 2, line);
}

inline JanusValue gate_x(uint32_t line) {
    return make_validated_gate({
        {0,0}, {1,0},
        {1,0}, {0,0}
    }, 2, line);
}

inline JanusValue gate_y(uint32_t line) {
    return make_validated_gate({
        {0,0}, {0,-1},
        {0,1}, {0,0}
    }, 2, line);
}

inline JanusValue gate_z(uint32_t line) {
    return make_validated_gate({
        {1,0}, {0,0},
        {0,0}, {-1,0}
    }, 2, line);
}

inline JanusValue gate_h(uint32_t line) {
    double r = 1.0 / std::sqrt(2.0);
    return make_validated_gate({
        {r,0}, {r,0},
        {r,0}, {-r,0}
    }, 2, line);
}

inline JanusValue gate_s(uint32_t line) {
    return make_validated_gate({
        {1,0}, {0,0},
        {0,0}, {0,1}
    }, 2, line);
}

inline JanusValue gate_sdg(uint32_t line) {
    return make_validated_gate({
        {1,0}, {0,0},
        {0,0}, {0,-1}
    }, 2, line);
}

inline JanusValue gate_t(uint32_t line) {
    double r = 1.0 / std::sqrt(2.0);
    return make_validated_gate({
        {1,0}, {0,0},
        {0,0}, {r,r}     // e^(i*pi/4) = cos(pi/4) + i*sin(pi/4)
    }, 2, line);
}

inline JanusValue gate_tdg(uint32_t line) {
    double r = 1.0 / std::sqrt(2.0);
    return make_validated_gate({
        {1,0}, {0,0},
        {0,0}, {r,-r}    // e^(-i*pi/4)
    }, 2, line);
}

inline JanusValue gate_sx(uint32_t line) {
    // SX = (1/2) * [[1+i, 1-i], [1-i, 1+i]]
    cx a{0.5, 0.5};    // (1+i)/2
    cx b{0.5, -0.5};   // (1-i)/2
    return make_validated_gate({
        a, b,
        b, a
    }, 2, line);
}

inline JanusValue gate_sxdg(uint32_t line) {
    // SXdg = (1/2) * [[1-i, 1+i], [1+i, 1-i]]
    cx a{0.5, -0.5};   // (1-i)/2
    cx b{0.5, 0.5};    // (1+i)/2
    return make_validated_gate({
        a, b,
        b, a
    }, 2, line);
}


// 2-qubit non-parameterised gates.


inline JanusValue gate_cnot(uint32_t line) {
    return make_validated_gate({
        {1,0}, {0,0}, {0,0}, {0,0},
        {0,0}, {1,0}, {0,0}, {0,0},
        {0,0}, {0,0}, {0,0}, {1,0},
        {0,0}, {0,0}, {1,0}, {0,0}
    }, 4, line);
}

inline JanusValue gate_cy(uint32_t line) {
    return make_validated_gate({
        {1,0}, {0,0}, {0,0}, {0,0},
        {0,0}, {1,0}, {0,0}, {0,0},
        {0,0}, {0,0}, {0,0}, {0,-1},
        {0,0}, {0,0}, {0,1}, {0,0}
    }, 4, line);
}

inline JanusValue gate_cz(uint32_t line) {
    return make_validated_gate({
        {1,0}, {0,0}, {0,0}, {0,0},
        {0,0}, {1,0}, {0,0}, {0,0},
        {0,0}, {0,0}, {1,0}, {0,0},
        {0,0}, {0,0}, {0,0}, {-1,0}
    }, 4, line);
}

inline JanusValue gate_ch(uint32_t line) {
    double r = 1.0 / std::sqrt(2.0);
    return make_validated_gate({
        {1,0}, {0,0}, {0,0}, {0,0},
        {0,0}, {1,0}, {0,0}, {0,0},
        {0,0}, {0,0}, {r,0}, {r,0},
        {0,0}, {0,0}, {r,0}, {-r,0}
    }, 4, line);
}

inline JanusValue gate_swap(uint32_t line) {
    return make_validated_gate({
        {1,0}, {0,0}, {0,0}, {0,0},
        {0,0}, {0,0}, {1,0}, {0,0},
        {0,0}, {1,0}, {0,0}, {0,0},
        {0,0}, {0,0}, {0,0}, {1,0}
    }, 4, line);
}

inline JanusValue gate_iswap(uint32_t line) {
    return make_validated_gate({
        {1,0}, {0,0}, {0,0}, {0,0},
        {0,0}, {0,0}, {0,1}, {0,0},
        {0,0}, {0,1}, {0,0}, {0,0},
        {0,0}, {0,0}, {0,0}, {1,0}
    }, 4, line);
}


// 3-qubit non-parameterised gates.


inline JanusValue gate_toffoli(uint32_t line) {
    // CCX: flips qubit 2 when qubits 0 and 1 are both |1>.
    // Identity on all states except |110> <-> |111>.
    std::vector<cx> data(64, {0,0});
    for (uint32_t i = 0; i < 8; ++i) {
        data[i * 8 + i] = {1, 0};
    }
    // Swap rows 6 and 7.
    data[6 * 8 + 6] = {0, 0};
    data[6 * 8 + 7] = {1, 0};
    data[7 * 8 + 7] = {0, 0};
    data[7 * 8 + 6] = {1, 0};
    return make_validated_gate(std::move(data), 8, line);
}

inline JanusValue gate_cswap(uint32_t line) {
    // Fredkin: swaps qubits 1 and 2 when qubit 0 is |1>.
    // Identity on all states except |101> <-> |110>.
    std::vector<cx> data(64, {0,0});
    for (uint32_t i = 0; i < 8; ++i) {
        data[i * 8 + i] = {1, 0};
    }
    // Swap rows 5 and 6.
    data[5 * 8 + 5] = {0, 0};
    data[5 * 8 + 6] = {1, 0};
    data[6 * 8 + 6] = {0, 0};
    data[6 * 8 + 5] = {1, 0};
    return make_validated_gate(std::move(data), 8, line);
}


// 1-qubit parameterised gates.


// RX(theta) = exp(-i*theta/2 * X)
inline JanusValue gate_rx(double theta, uint32_t line) {
    double c = std::cos(theta / 2.0);
    double s = std::sin(theta / 2.0);
    return make_validated_gate({
        {c, 0}, {0, -s},
        {0, -s}, {c, 0}
    }, 2, line);
}

// RY(theta) = exp(-i*theta/2 * Y)
inline JanusValue gate_ry(double theta, uint32_t line) {
    double c = std::cos(theta / 2.0);
    double s = std::sin(theta / 2.0);
    return make_validated_gate({
        {c, 0}, {-s, 0},
        {s, 0}, {c, 0}
    }, 2, line);
}

// RZ(theta) = exp(-i*theta/2 * Z)
inline JanusValue gate_rz(double theta, uint32_t line) {
    double ht = theta / 2.0;
    cx e_neg = {std::cos(-ht), std::sin(-ht)};   // e^(-i*theta/2)
    cx e_pos = {std::cos(ht), std::sin(ht)};     // e^(i*theta/2)
    return make_validated_gate({
        e_neg, {0,0},
        {0,0}, e_pos
    }, 2, line);
}

// P(theta): phase gate [[1,0],[0,e^(i*theta)]]
inline JanusValue gate_p(double theta, uint32_t line) {
    cx phase = {std::cos(theta), std::sin(theta)};
    return make_validated_gate({
        {1,0}, {0,0},
        {0,0}, phase
    }, 2, line);
}

// U(theta, phi, lambda): general single-qubit unitary.
// [[cos(t/2), -e^(i*l)*sin(t/2)],
//  [e^(i*p)*sin(t/2), e^(i*(p+l))*cos(t/2)]]
inline JanusValue gate_u(double theta, double phi, double lambda,
                         uint32_t line) {
    double c = std::cos(theta / 2.0);
    double s = std::sin(theta / 2.0);
    cx e_il  = {std::cos(lambda), std::sin(lambda)};
    cx e_ip  = {std::cos(phi), std::sin(phi)};
    cx e_ipl = {std::cos(phi + lambda), std::sin(phi + lambda)};
    return make_validated_gate({
        {c, 0},       -e_il * s,
        e_ip * s,     e_ipl * c
    }, 2, line);
}


// 2-qubit parameterised gates.


// CRX(theta): controlled RX.
// Upper-left 2x2 block is identity, lower-right 2x2 block is RX(theta).
inline JanusValue gate_crx(double theta, uint32_t line) {
    double c = std::cos(theta / 2.0);
    double s = std::sin(theta / 2.0);
    return make_validated_gate({
        {1,0}, {0,0}, {0,0},  {0,0},
        {0,0}, {1,0}, {0,0},  {0,0},
        {0,0}, {0,0}, {c,0},  {0,-s},
        {0,0}, {0,0}, {0,-s}, {c,0}
    }, 4, line);
}

// CRY(theta): controlled RY.
inline JanusValue gate_cry(double theta, uint32_t line) {
    double c = std::cos(theta / 2.0);
    double s = std::sin(theta / 2.0);
    return make_validated_gate({
        {1,0}, {0,0}, {0,0}, {0,0},
        {0,0}, {1,0}, {0,0}, {0,0},
        {0,0}, {0,0}, {c,0}, {-s,0},
        {0,0}, {0,0}, {s,0}, {c,0}
    }, 4, line);
}

// CRZ(theta): controlled RZ.
inline JanusValue gate_crz(double theta, uint32_t line) {
    double ht = theta / 2.0;
    cx e_neg = {std::cos(-ht), std::sin(-ht)};
    cx e_pos = {std::cos(ht), std::sin(ht)};
    return make_validated_gate({
        {1,0}, {0,0}, {0,0}, {0,0},
        {0,0}, {1,0}, {0,0}, {0,0},
        {0,0}, {0,0}, e_neg, {0,0},
        {0,0}, {0,0}, {0,0}, e_pos
    }, 4, line);
}

// CP(theta): controlled phase.
inline JanusValue gate_cp(double theta, uint32_t line) {
    cx phase = {std::cos(theta), std::sin(theta)};
    return make_validated_gate({
        {1,0}, {0,0}, {0,0}, {0,0},
        {0,0}, {1,0}, {0,0}, {0,0},
        {0,0}, {0,0}, {1,0}, {0,0},
        {0,0}, {0,0}, {0,0}, phase
    }, 4, line);
}

// XX(theta): Ising XX interaction, exp(-i * theta/2 * X tensor X).
inline JanusValue gate_xx(double theta, uint32_t line) {
    double c = std::cos(theta / 2.0);
    double s = std::sin(theta / 2.0);
    return make_validated_gate({
        {c,0},  {0,0},  {0,0},  {0,-s},
        {0,0},  {c,0},  {0,-s}, {0,0},
        {0,0},  {0,-s}, {c,0},  {0,0},
        {0,-s}, {0,0},  {0,0},  {c,0}
    }, 4, line);
}

// YY(theta): Ising YY interaction, exp(-i * theta/2 * Y tensor Y).
inline JanusValue gate_yy(double theta, uint32_t line) {
    double c = std::cos(theta / 2.0);
    double s = std::sin(theta / 2.0);
    return make_validated_gate({
        {c,0},  {0,0},  {0,0},  {0,s},
        {0,0},  {c,0},  {0,-s}, {0,0},
        {0,0},  {0,-s}, {c,0},  {0,0},
        {0,s},  {0,0},  {0,0},  {c,0}
    }, 4, line);
}

// ZZ(theta): Ising ZZ interaction, exp(-i * theta/2 * Z tensor Z).
inline JanusValue gate_zz(double theta, uint32_t line) {
    double ht = theta / 2.0;
    cx e_neg = {std::cos(-ht), std::sin(-ht)};   // e^(-i*theta/2)
    cx e_pos = {std::cos(ht), std::sin(ht)};     // e^(i*theta/2)
    return make_validated_gate({
        e_neg, {0,0}, {0,0}, {0,0},
        {0,0}, e_pos, {0,0}, {0,0},
        {0,0}, {0,0}, e_pos, {0,0},
        {0,0}, {0,0}, {0,0}, e_neg
    }, 4, line);
}


// Returns true if name is a recognised predefined gate.
inline bool is_predefined_gate(const std::string& name) {
    return name == "i"      || name == "x"      || name == "y"     ||
           name == "z"      || name == "h"      || name == "s"     ||
           name == "sdg"    || name == "t"      || name == "tdg"   ||
           name == "sx"     || name == "sxdg"   ||
           name == "cnot"   || name == "cy"     || name == "cz"    ||
           name == "ch"     || name == "swap"   || name == "iswap" ||
           name == "toffoli"|| name == "cswap"  ||
           name == "rx"     || name == "ry"     || name == "rz"    ||
           name == "p"      || name == "u"      ||
           name == "crx"    || name == "cry"    || name == "crz"   ||
           name == "cp"     ||
           name == "xx"     || name == "yy"     || name == "zz";
}

// Returns the expected argument count for a predefined gate.
// Non-parameterised gates return 0.
inline uint32_t gate_param_count(const std::string& name) {
    if (name == "u") return 3;
    if (name == "rx"  || name == "ry"  || name == "rz"  ||
        name == "p"   ||
        name == "crx" || name == "cry" || name == "crz" ||
        name == "cp"  ||
        name == "xx"  || name == "yy"  || name == "zz") {
        return 1;
    }
    return 0;
}

// Resolves a predefined gate by name and evaluated arguments.
// All parameter values must already be extracted as doubles via
// as_real on the JanusValue arguments.
inline JanusValue resolve_gate(const std::string& name,
                               const std::vector<JanusValue>& args,
                               uint32_t line) {
    uint32_t expected = gate_param_count(name);
    if (args.size() != expected) {
        report_error(line);
    }

    // Non-parameterised 1-qubit gates.
    if (name == "i")     return gate_i(line);
    if (name == "x")     return gate_x(line);
    if (name == "y")     return gate_y(line);
    if (name == "z")     return gate_z(line);
    if (name == "h")     return gate_h(line);
    if (name == "s")     return gate_s(line);
    if (name == "sdg")   return gate_sdg(line);
    if (name == "t")     return gate_t(line);
    if (name == "tdg")   return gate_tdg(line);
    if (name == "sx")    return gate_sx(line);
    if (name == "sxdg")  return gate_sxdg(line);

    // Non-parameterised 2-qubit gates.
    if (name == "cnot")  return gate_cnot(line);
    if (name == "cy")    return gate_cy(line);
    if (name == "cz")    return gate_cz(line);
    if (name == "ch")    return gate_ch(line);
    if (name == "swap")  return gate_swap(line);
    if (name == "iswap") return gate_iswap(line);

    // Non-parameterised 3-qubit gates.
    if (name == "toffoli") return gate_toffoli(line);
    if (name == "cswap")   return gate_cswap(line);

    // Parameterised 1-qubit gates.
    if (name == "rx") return gate_rx(args[0].as_real(line), line);
    if (name == "ry") return gate_ry(args[0].as_real(line), line);
    if (name == "rz") return gate_rz(args[0].as_real(line), line);
    if (name == "p")  return gate_p(args[0].as_real(line), line);
    if (name == "u") {
        return gate_u(args[0].as_real(line),
                      args[1].as_real(line),
                      args[2].as_real(line), line);
    }

    // Parameterised 2-qubit gates.
    if (name == "crx") return gate_crx(args[0].as_real(line), line);
    if (name == "cry") return gate_cry(args[0].as_real(line), line);
    if (name == "crz") return gate_crz(args[0].as_real(line), line);
    if (name == "cp")  return gate_cp(args[0].as_real(line), line);
    if (name == "xx")  return gate_xx(args[0].as_real(line), line);
    if (name == "yy")  return gate_yy(args[0].as_real(line), line);
    if (name == "zz")  return gate_zz(args[0].as_real(line), line);

    // Unrecognised gate name.
    report_error(line);
}

} // namespace gate_library
} // namespace janus

#endif // JANUS_GATE_LIBRARY_HPP
