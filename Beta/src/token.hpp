#ifndef JANUS_TOKEN_HPP
#define JANUS_TOKEN_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace janus {

enum class TokenType : uint8_t {

    // Literal tokens

    INTEGER_LITERAL,        // 5, 0, 42
    FLOAT_LITERAL,          // 5.5, 3.14, 0.707
    STRING_LITERAL,         // "hello" (complete string, no interpolation)
    STRING_INTERP_BEGIN,    // "text ${ (opening segment of an interpolated string)
    STRING_INTERP_MID,      // } text ${ (middle segment between two interpolations)
    STRING_INTERP_END,      // } text" (closing segment of an interpolated string)


    // Identifier

    IDENTIFIER,


    // Type keywords
    // (Beta types: qubit, cbit, qnum, cnum, cstr, list, matrix,
    //  gate, circ, block, function)

    KW_QUBIT,
    KW_CBIT,
    KW_QNUM,
    KW_CNUM,
    KW_CSTR,
    KW_LIST,
    KW_MATRIX,
    KW_GATE,
    KW_CIRC,
    KW_BLOCK,
    KW_FUNCTION,


    // Control flow keywords
    // (Beta control flow: if/else/else if, for, while, foreach)

    KW_IF,
    KW_ELSE,
    KW_FOR,
    KW_WHILE,
    KW_FOREACH,
    KW_IN,          // foreach(x in collection)
    KW_WHERE,       // foreach ... where Condition
    KW_FROM,        // foreach ... from Lower
    KW_TO,          // foreach ... to Upper
    KW_BREAK,
    KW_CONTINUE,
    KW_RETURN,


    // Literal value keywords

    KW_TRUE,
    KW_FALSE,
    KW_NULL,


    // Keyword operators from the precedence table
    // (Bitwise logic at level 13; boolean/bitwise negation at level 5;
    //  tensor product at level 7)

    KW_AND,         // bitwise and  (precedence 13)
    KW_NAND,        // bitwise nand (precedence 13)
    KW_OR,          // bitwise or   (precedence 13)
    KW_NOR,         // bitwise nor  (precedence 13)
    KW_XOR,         // bitwise xor  (precedence 13)
    KW_XNOR,        // bitwise xnor (precedence 13)
    KW_NOT,         // bitwise negation (precedence 5, unary prefix)
    KW_TENSOR,      // tensor product   (precedence 7, binary)


    // Built-in operand keywords (Beta operand list)
    // Function-call style operands parsed at precedence level 3

    KW_MEASURE,         // measure(Register) or measure(Register, Basis)  (35)
    KW_PEEK,            // peek(Register)                                 (37)
    KW_STATE,           // state(Register)                                (55)
    KW_EXPECT,          // expect(Matrix, Register)                       (38)
    KW_CTRLE,           // ctrle(Gate, ControlQubits)                     (57)
    KW_RUN,             // run(Circuit, ...)                              (64)
    KW_RUNH,            // runh(Circuit, ...)                             (65)
    KW_ISUNITARY,       // isunitary(Matrix)                              (97)
    KW_SAMEOUTPUT,      // sameoutput(Circ1, Circ2)                      (96)
    KW_PRINT,           // print(...)                                     (54)
    KW_DELETE,          // delete(Collection, Index) or delete(C, Elem)   (43)
    KW_SIN,             // sin(x)                                         (76)
    KW_COS,             // cos(x)                                         (77)
    KW_NUMBEROFGATES,   // numberofgates(Circ)                           (103)


    // Built-in operand keywords: unary prefix (precedence level 5)

    KW_DET,             // det(Matrix)        determinant                 (47)
    KW_TRANSPOSE,       // transpose Matrix   transpose                   (60)
    KW_TRANSPOSEC,      // transposec Matrix  conjugate transpose         (61)
    KW_EVALS,           // evals Matrix       eigenvalues                 (58)
    KW_EVECS,           // evecs Matrix       eigenvectors                (59)


    // Built-in operand keywords: circuit inspection (precedence 5)

    KW_GATES,           // gates(Circuit) list of gates                   (68)
                        // Also the namespace prefix for gate library:
                        // gates.x(), gates.h(), etc.
    KW_QUBITS,          // qubits(Circuit) qubit count/list              (69)
    KW_DEPTH,           // depth(Circuit)  circuit depth                  (70)
    KW_BITLENGTH,       // bitlength(Register) qubit count of qnum/qubit


    // Built-in constant keywords

    KW_PI,              // pi  (78)
    KW_E,               // e   (79)


    // Arithmetic operators

    PLUS,               // +   addition (1) / concatenation (5)
    MINUS,              // -   subtraction (2) / unary negation
    STAR,               // *   multiplication (3)
    SLASH,              // /   division (4)
    DOUBLE_SLASH,       // //  integer division (20)
    PERCENT,            // %   modulus (6)
    CARET,              // ^   exponentiation (7), right-associative


    // Comparison operators

    LESS,               // <   less-than (16)
    GREATER,            // >   greater-than (17)
    LESS_EQUAL,         // <=  less-than-or-equal
    GREATER_EQUAL,      // >=  greater-than-or-equal
    EQUAL_EQUAL,        // ==  equality (11)


    // Assignment operator

    EQUAL,              // =   assignment (precedence 17), right-associative


    // Boolean negation symbol

    BANG,               // !   boolean negation (15), prefix and postfix


    // Unary prefix shift operators (always unary, never binary)

    SHIFT_LEFT,         // <<  unary prefix (precedence 5)
    SHIFT_RIGHT,        // >>  unary prefix (precedence 5)


    // Delimiters

    LEFT_PAREN,         // (
    RIGHT_PAREN,        // )
    LEFT_BRACE,         // {
    RIGHT_BRACE,        // }
    LEFT_BRACKET,       // [
    RIGHT_BRACKET,      // ]
    COMMA,              // ,
    SEMICOLON,          // ;   (for-loop header separator; matrix row separator)
    DOT,                // .   (gates.x() namespace access; struct.field)


    // Special tokens

    NEWLINE,            // statement terminator (commands split by newline)
    EOF_TOKEN,          // end of source file
    ERROR               // lexer error token
};

struct Token {
    TokenType   type;
    std::string lexeme;
    uint32_t    line;
};

// ---------------------------------------------------------------------------
// Utility: (for diagnostics)
// ---------------------------------------------------------------------------
constexpr std::string_view token_type_name(TokenType t) noexcept {
    switch (t) {
        case TokenType::INTEGER_LITERAL:      return "INTEGER_LITERAL";
        case TokenType::FLOAT_LITERAL:        return "FLOAT_LITERAL";
        case TokenType::STRING_LITERAL:       return "STRING_LITERAL";
        case TokenType::STRING_INTERP_BEGIN:  return "STRING_INTERP_BEGIN";
        case TokenType::STRING_INTERP_MID:    return "STRING_INTERP_MID";
        case TokenType::STRING_INTERP_END:    return "STRING_INTERP_END";
        case TokenType::IDENTIFIER:           return "IDENTIFIER";
        case TokenType::KW_QUBIT:             return "KW_QUBIT";
        case TokenType::KW_CBIT:              return "KW_CBIT";
        case TokenType::KW_QNUM:             return "KW_QNUM";
        case TokenType::KW_CNUM:             return "KW_CNUM";
        case TokenType::KW_CSTR:              return "KW_CSTR";
        case TokenType::KW_LIST:              return "KW_LIST";
        case TokenType::KW_MATRIX:            return "KW_MATRIX";
        case TokenType::KW_GATE:              return "KW_GATE";
        case TokenType::KW_CIRC:              return "KW_CIRC";
        case TokenType::KW_BLOCK:             return "KW_BLOCK";
        case TokenType::KW_FUNCTION:          return "KW_FUNCTION";
        case TokenType::KW_IF:                return "KW_IF";
        case TokenType::KW_ELSE:              return "KW_ELSE";
        case TokenType::KW_FOR:               return "KW_FOR";
        case TokenType::KW_WHILE:             return "KW_WHILE";
        case TokenType::KW_FOREACH:           return "KW_FOREACH";
        case TokenType::KW_IN:                return "KW_IN";
        case TokenType::KW_WHERE:             return "KW_WHERE";
        case TokenType::KW_FROM:              return "KW_FROM";
        case TokenType::KW_TO:                return "KW_TO";
        case TokenType::KW_BREAK:             return "KW_BREAK";
        case TokenType::KW_CONTINUE:          return "KW_CONTINUE";
        case TokenType::KW_RETURN:            return "KW_RETURN";
        case TokenType::KW_TRUE:              return "KW_TRUE";
        case TokenType::KW_FALSE:             return "KW_FALSE";
        case TokenType::KW_NULL:              return "KW_NULL";
        case TokenType::KW_AND:               return "KW_AND";
        case TokenType::KW_NAND:              return "KW_NAND";
        case TokenType::KW_OR:                return "KW_OR";
        case TokenType::KW_NOR:               return "KW_NOR";
        case TokenType::KW_XOR:               return "KW_XOR";
        case TokenType::KW_XNOR:              return "KW_XNOR";
        case TokenType::KW_NOT:               return "KW_NOT";
        case TokenType::KW_TENSOR:            return "KW_TENSOR";
        case TokenType::KW_MEASURE:           return "KW_MEASURE";
        case TokenType::KW_PEEK:              return "KW_PEEK";
        case TokenType::KW_STATE:             return "KW_STATE";
        case TokenType::KW_EXPECT:            return "KW_EXPECT";
        case TokenType::KW_CTRLE:             return "KW_CTRLE";
        case TokenType::KW_RUN:               return "KW_RUN";
        case TokenType::KW_RUNH:              return "KW_RUNH";
        case TokenType::KW_ISUNITARY:         return "KW_ISUNITARY";
        case TokenType::KW_SAMEOUTPUT:        return "KW_SAMEOUTPUT";
        case TokenType::KW_PRINT:             return "KW_PRINT";
        case TokenType::KW_DELETE:            return "KW_DELETE";
        case TokenType::KW_SIN:               return "KW_SIN";
        case TokenType::KW_COS:               return "KW_COS";
        case TokenType::KW_NUMBEROFGATES:     return "KW_NUMBEROFGATES";
        case TokenType::KW_DET:               return "KW_DET";
        case TokenType::KW_TRANSPOSE:         return "KW_TRANSPOSE";
        case TokenType::KW_TRANSPOSEC:        return "KW_TRANSPOSEC";
        case TokenType::KW_EVALS:             return "KW_EVALS";
        case TokenType::KW_EVECS:             return "KW_EVECS";
        case TokenType::KW_GATES:             return "KW_GATES";
        case TokenType::KW_QUBITS:            return "KW_QUBITS";
        case TokenType::KW_DEPTH:             return "KW_DEPTH";
        case TokenType::KW_BITLENGTH:         return "KW_BITLENGTH";
        case TokenType::KW_PI:                return "KW_PI";
        case TokenType::KW_E:                 return "KW_E";
        case TokenType::PLUS:                 return "PLUS";
        case TokenType::MINUS:                return "MINUS";
        case TokenType::STAR:                 return "STAR";
        case TokenType::SLASH:                return "SLASH";
        case TokenType::DOUBLE_SLASH:         return "DOUBLE_SLASH";
        case TokenType::PERCENT:              return "PERCENT";
        case TokenType::CARET:                return "CARET";
        case TokenType::LESS:                 return "LESS";
        case TokenType::GREATER:              return "GREATER";
        case TokenType::LESS_EQUAL:           return "LESS_EQUAL";
        case TokenType::GREATER_EQUAL:        return "GREATER_EQUAL";
        case TokenType::EQUAL_EQUAL:          return "EQUAL_EQUAL";
        case TokenType::EQUAL:                return "EQUAL";
        case TokenType::BANG:                 return "BANG";
        case TokenType::SHIFT_LEFT:           return "SHIFT_LEFT";
        case TokenType::SHIFT_RIGHT:          return "SHIFT_RIGHT";
        case TokenType::LEFT_PAREN:           return "LEFT_PAREN";
        case TokenType::RIGHT_PAREN:          return "RIGHT_PAREN";
        case TokenType::LEFT_BRACE:           return "LEFT_BRACE";
        case TokenType::RIGHT_BRACE:          return "RIGHT_BRACE";
        case TokenType::LEFT_BRACKET:         return "LEFT_BRACKET";
        case TokenType::RIGHT_BRACKET:        return "RIGHT_BRACKET";
        case TokenType::COMMA:                return "COMMA";
        case TokenType::SEMICOLON:            return "SEMICOLON";
        case TokenType::DOT:                  return "DOT";
        case TokenType::NEWLINE:              return "NEWLINE";
        case TokenType::EOF_TOKEN:            return "EOF_TOKEN";
        case TokenType::ERROR:                return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace janus

#endif // JANUS_TOKEN_HPP
