#ifndef JANUS_TYPECHECKER_HPP
#define JANUS_TYPECHECKER_HPP

#include "ast.hpp"
#include "types.hpp"
#include "error.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace janus {

// TypeChecker :: compile-time type checking and annotation.
//
// The type checker walks the AST produced by the parser and:
//   1. Resolves the type of every expression node, storing the result in
//      a map from Expr* to TypeInfo (the "annotated AST").
//   2. Enforces all compile-time type rules from the documentation:
//        - Operator allowed types (arithmetic, comparison, bitwise, etc.).
//        - Assignment type consistency: re-assigning a variable in the
//          same scope to a different type is an error unless a type cast
//          is applied.
//        - Type cast validation via is_cast_allowed.
//        - Function isolation: variables from enclosing or global scopes
//          are not accessible inside function bodies.
//        - Loop variable persistence: for init variables and foreach
//          element variables persist into the enclosing scope.
//        - Loop variable outer scope collision: declaring a loop variable
//          whose name already exists in the enclosing scope frame is an
//          error.
//        - break/continue only inside loops.
//        - Top-level return must have an integer value or no value.
//   3. Performs a forward-declaration pre-pass at each scope level so
//      that functions may be called before their source-order declaration.
//
// On any type error the single Beta error is reported via report_error
// and the process terminates.

class TypeChecker {
public:
    TypeChecker();

    // Type-checks the entire program.  On any error calls report_error
    // which terminates the process.
    void check(const Program& program);

    // Returns the resolved TypeInfo for an AST expression node.
    // Must only be called after check() has completed successfully.
    // Returns a null TypeInfo if the node was not annotated (should not
    // happen for a well-formed AST after a successful check).
    TypeInfo get_type(const Expr* expr) const;

private:

    // Compile-time scope frame.  Mirrors the structure of the runtime
    // ScopeFrame but stores TypeInfo instead of JanusValue.
    struct TypeFrame {
        std::unordered_map<std::string, TypeInfo> vars;
        bool is_function_boundary = false;
    };

    // Scope stack.  Frame 0 is the script-level scope.
    std::vector<TypeFrame> frames_;

    // Type annotation map: maps each AST Expr* to its resolved TypeInfo.
    std::unordered_map<const Expr*, TypeInfo> type_map_;

    // Context tracking.
    uint32_t loop_depth_     = 0;
    uint32_t function_depth_ = 0;


    // Compile-time scope management.

    void push_block();
    void pop_block();
    void push_function();
    void pop_function();

    // Looks up a variable by name, searching from the innermost frame
    // outward and stopping at function boundaries.  Returns nullptr if
    // not found.
    TypeInfo* lookup(const std::string& name);

    // Assigns a variable in the current (innermost) scope frame.
    // If the variable already exists in the current frame, performs
    // re-assignment type checking.  Otherwise creates a new variable
    // (shadowing any outer-scope variable of the same name).
    void assign(const std::string& name, TypeInfo type, uint32_t line);

    // Declares a loop variable in the current (enclosing) scope frame.
    // Checks for collision with an existing variable in the same frame.
    void declare_loop_variable(const std::string& name, TypeInfo type,
                               uint32_t line);

    // Returns true if the name exists in the current (innermost) frame.
    bool exists_in_current_frame(const std::string& name) const;


    // Forward-declaration pre-pass.

    // Scans a list of statements for top-level function assignments
    // (pattern: identifier = function(...) { ... }) and registers them
    // in the current scope frame so that forward calls are valid.
    void forward_declare_functions(const std::vector<StmtPtr>& stmts);


    // Statement checking.

    void check_stmt(const Stmt& stmt);
    void check_stmts(const std::vector<StmtPtr>& stmts);


    // Expression checking.
    // Each method resolves the type of the expression, stores it via
    // annotate(), and returns the TypeInfo.

    TypeInfo check_expr(const Expr& expr);

    TypeInfo check_integer_literal(const IntegerLiteralExpr& e);
    TypeInfo check_float_literal(const FloatLiteralExpr& e);
    TypeInfo check_string_literal(const StringLiteralExpr& e);
    TypeInfo check_bool_literal(const BoolLiteralExpr& e);
    TypeInfo check_null_literal(const NullLiteralExpr& e);
    TypeInfo check_pi_literal(const PiLiteralExpr& e);
    TypeInfo check_e_literal(const ELiteralExpr& e);
    TypeInfo check_interpolated_string(const InterpolatedStringExpr& e);
    TypeInfo check_identifier(const IdentifierExpr& e);
    TypeInfo check_index(const IndexExpr& e);
    TypeInfo check_member_access(const MemberAccessExpr& e);
    TypeInfo check_assign(const AssignExpr& e);
    TypeInfo check_binary(const BinaryExpr& e);
    TypeInfo check_unary(const UnaryExpr& e);
    TypeInfo check_postfix_bang(const PostfixBangExpr& e);
    TypeInfo check_type_cast(const TypeCastExpr& e);
    TypeInfo check_matrix_literal(const MatrixLiteralExpr& e);
    TypeInfo check_call(const CallExpr& e);
    TypeInfo check_gate_library(const GateLibraryExpr& e);
    TypeInfo check_type_construct(const TypeConstructExpr& e);
    TypeInfo check_function_expr(const FunctionExpr& e);
    TypeInfo check_builtin_call(const BuiltinCallExpr& e);
    TypeInfo check_if(const IfExpr& e);
    TypeInfo check_for(const ForExpr& e);
    TypeInfo check_while(const WhileExpr& e);
    TypeInfo check_foreach(const ForeachExpr& e);


    // Annotation helper.

    void annotate(const Expr& expr, TypeInfo type);


    // Operator type validation helpers.

    // Returns true if the type is allowed as an operand to arithmetic
    // operators (+, -, *, /, //, %, ^) in the Beta.
    // Allowed: qubit, cbit, qnum, cnum, cstr, list, matrix, null.
    static bool is_arithmetic_allowed(JanusType t);

    // Returns true if the type is allowed as an operand to comparison
    // operators (<, >, <=, >=) in the Beta.
    // Allowed: qubit, cbit, qnum, cnum, cstr, list, matrix, block,
    //          circ, gate, null.
    static bool is_comparison_allowed(JanusType t);

    // Returns true if the type is allowed as an operand to the equality
    // operator (==) in the Beta.
    // Allowed: qubit, cbit, qnum, cnum, list, matrix, block, circ, gate,
    //          null.
    static bool is_equality_allowed(JanusType t);

    // Returns true if the type is allowed as an operand to bitwise logic
    // operators (and, nand, or, nor, xor, xnor, not, <<, >>) in Beta.
    // Allowed: qubit, cbit, qnum, cnum, cstr, list, matrix, null.
    static bool is_bitwise_allowed(JanusType t);

    // Returns true if the type is allowed as an operand to boolean
    // negation (! prefix or postfix) in the Beta.
    // Allowed: qubit, cbit, qnum, cnum, cstr, list, matrix, null.
    static bool is_bool_negation_allowed(JanusType t);

    // Returns true if the type is allowed as an operand to tensor
    // product in the Beta.
    // Allowed: qubit, cbit, qnum, cnum, cstr, list, matrix, null.
    static bool is_tensor_allowed(JanusType t);

    // Computes the result type for an arithmetic binary operation,
    // extending arithmetic_result_type from types.hpp to handle CSTR
    // (treated as CNUM via 2's complement register value interpretation).
    static TypeInfo resolve_arithmetic_type(JanusType lhs, JanusType rhs,
                                            uint32_t line);

    // Returns the result type for boolean negation (! or postfix !).
    // CBIT for classical operands, QUBIT for quantum operands.
    static TypeInfo resolve_bool_negation_type(JanusType operand);

    // Returns the result type for the tensor product.
    static TypeInfo resolve_tensor_type(JanusType lhs, JanusType rhs);
};

} // namespace janus

#endif // JANUS_TYPECHECKER_HPP
