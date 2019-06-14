#include <algorithm>

#include "check.h"
#include "log.h"

namespace artic {

bool TypeChecker::run(const ast::Program& program) {
    program.infer(*this);
    return error_count == 0;
}

bool TypeChecker::should_emit_error(Type type) {
    return !type.contains(error_type());
}

artic::Type TypeChecker::expect(const Loc& loc, const std::string& msg, Type type, Type expected) {
    if (auto meet = Type::meet(type, expected))
        return meet;
    if (should_emit_error(type))
        error(loc, "expected type '{}', but got {} with type '{}'", expected, msg, type);
    return error_type();
}

artic::Type TypeChecker::expect(const Loc& loc, const std::string& msg, Type expected) {
    error(loc, "expected type '{}', but got {}", expected, msg);
    return error_type();
}

artic::Type TypeChecker::expect(const Loc& loc, Type type, Type expected) {
    if (auto meet = Type::meet(type, expected))
        return meet;
    if (should_emit_error(type))
        error(loc, "expected type '{}', but got type '{}'", expected, type);
    return error_type();
}

artic::Type TypeChecker::cannot_infer(const Loc& loc, const std::string& msg) {
    error(loc, "cannot infer type for {}", msg);
    return error_type();
}

artic::Type TypeChecker::unreachable_code(const Loc& before, const Loc& first, const Loc& last) {
    error(Loc(first, last), "unreachable code");
    note(before, "after this statement");
    return error_type();
}

Type TypeChecker::check(const ast::Node& node, Type type) {
    assert(!node.type); // Nodes can only be visited once
    return node.type = node.check(*this, type);
}

Type TypeChecker::infer(const ast::Node& node) {
    if (node.type)
        return node.type;
    return node.type = node.infer(*this);
}

template <typename Args>
Type TypeChecker::check_tuple(const Loc& loc, const std::string& msg, const Args& args, Type expected) {
    if (!expected.isa<TupleType>())
        return expect(loc, msg, expected);
    if (args.size() != expected.as<TupleType>().num_args()) {
        error(loc, "expected {} argument(s) in {}, but got {}", expected.as<TupleType>().num_args(), msg, args.size());
        return error_type();
    }
    for (size_t i = 0; i < args.size(); ++i)
        check(*args[i], expected.as<TupleType>().arg(i));
    return expected;
}

template <typename Args>
Type TypeChecker::infer_tuple(const Args& args) {
    thorin::Array<artic::Type> arg_types(args.size());
    for (size_t i = 0; i < args.size(); ++i)
        arg_types[i] = infer(*args[i]);
    return tuple_type(arg_types);
}

namespace ast {

artic::Type Node::check(TypeChecker& checker, artic::Type expected) const {
    // By default, try to infer, and then check that types match
    return checker.expect(loc, checker.infer(*this), expected);
}

artic::Type Node::infer(TypeChecker& checker) const {
    return checker.cannot_infer(loc, "expression");
}

artic::Type Path::infer(TypeChecker& checker) const {
    if (!symbol || symbol->decls.empty())
        return checker.error_type();
    return checker.infer(*symbol->decls.front());
}

// Types ---------------------------------------------------------------------------

artic::Type PrimType::infer(TypeChecker& checker) const {
    return checker.prim_type(static_cast<artic::PrimType::Tag>(tag));
}

artic::Type TupleType::infer(TypeChecker& checker) const {
    return checker.infer_tuple(args);
}

artic::Type ArrayType::infer(TypeChecker& checker) const {
    return checker.array_type(checker.infer(*elem));
}

artic::Type FnType::infer(TypeChecker& checker) const {
    return checker.fn_type(checker.infer(*from), checker.infer(*to));
}

// Statements ----------------------------------------------------------------------

artic::Type DeclStmt::infer(TypeChecker& checker) const {
    return checker.infer(*decl);
}

artic::Type DeclStmt::check(TypeChecker& checker, artic::Type expected) const {
    return checker.check(*decl, expected);
}

artic::Type ExprStmt::infer(TypeChecker& checker) const {
    return checker.infer(*expr);
}

artic::Type ExprStmt::check(TypeChecker& checker, artic::Type expected) const {
    return checker.check(*expr, expected);
}

// Expressions ---------------------------------------------------------------------

artic::Type PathExpr::infer(TypeChecker& checker) const {
    return checker.infer(path);
}

artic::Type TupleExpr::infer(TypeChecker& checker) const {
    return checker.infer_tuple(args);
}

artic::Type TupleExpr::check(TypeChecker& checker, artic::Type expected) const {
    return checker.check_tuple(loc, "tuple expression", args, expected);
}

artic::Type ArrayExpr::infer(TypeChecker& checker) const {
    if (elems.empty())
        return checker.cannot_infer(loc, "array expression");
    auto elem_type = checker.infer(*elems.front());
    for (size_t i = 1; i < elems.size(); ++i)
        checker.check(*elems[i], elem_type);
    return checker.array_type(elem_type);
}

artic::Type ArrayExpr::check(TypeChecker& checker, artic::Type expected) const {
    if (!expected.isa<artic::ArrayType>())
        return checker.expect(loc, "array expression", expected);
    auto elem_type = expected.as<artic::ArrayType>().elem();
    for (auto& elem : elems)
        checker.check(*elem, elem_type);
    return checker.array_type(elem_type);
}

artic::Type FnExpr::infer(TypeChecker& checker) const {
    auto body_type = ret_type ? checker.infer(*ret_type) : artic::Type();
    if (body || body_type) {
        auto param_type = checker.infer(*param);
        body_type = body_type ? checker.check(*body, body_type) : checker.infer(*body);
        return checker.fn_type(param_type, body_type);
    }
    return checker.cannot_infer(loc, "function");
}

artic::Type FnExpr::check(TypeChecker& checker, artic::Type expected) const {
    if (!expected.isa<artic::FnType>())
        return checker.expect(loc, "anonymous function", expected);
    checker.check(*param, expected.as<artic::FnType>().from());
    return checker.check(*body, expected.as<artic::FnType>().to());
}

artic::Type BlockExpr::infer(TypeChecker& checker) const {
    if (stmts.empty())
        return checker.unit_type();
    for (size_t i = 0; i < stmts.size() - 1; ++i) {
        auto stmt_type = checker.check(*stmts[i], checker.unit_type()).isa<NoRetType>();
        if (stmt_type.isa<artic::NoRetType>())
            return checker.unreachable_code(stmts[i]->loc, stmts[i + 1]->loc, stmts.back()->loc);
    }
    return checker.infer(*stmts.back());
}

artic::Type BlockExpr::check(TypeChecker& checker, artic::Type expected) const {
    if (stmts.empty())
        return checker.expect(loc, "block expression", checker.unit_type(), expected);
    for (size_t i = 0; i < stmts.size() - 1; ++i) {
        auto stmt_type = checker.check(*stmts[i], checker.unit_type());
        if (stmt_type.isa<artic::NoRetType>())
            return checker.unreachable_code(stmts[i]->loc, stmts[i + 1]->loc, stmts.back()->loc);
    }
    return checker.check(*stmts.back(), expected);
}

artic::Type CallExpr::infer(TypeChecker& checker) const {
    auto callee_type = checker.infer(*callee);
    if (auto fn_type = callee_type.isa<artic::FnType>()) {
        // TODO: Polymorphic functions
        checker.check(*arg, fn_type.from());
        return fn_type.to();
    } else if (callee_type.isa<artic::ArrayType>()) {
        // TODO
        return checker.error_type();
    } else {
        if (!callee_type.isa<artic::ErrorType>())
            checker.error(loc, "expected function or array type in call expression, but got '{}'", callee_type);
        return checker.error_type();
    }
}

artic::Type IfExpr::infer(TypeChecker& checker) const {
    checker.check(*cond, checker.prim_type(artic::PrimType::Bool));
    if (if_false)
        return checker.check(*if_false, checker.infer(*if_true));
    return checker.check(*if_true, checker.unit_type());
}

artic::Type IfExpr::check(TypeChecker& checker, artic::Type expected) const {
    checker.check(*cond, checker.prim_type(artic::PrimType::Bool));
    auto true_type = checker.check(*if_true, expected);
    if (if_false)
        return checker.check(*if_false, true_type);
    return true_type;
}

artic::Type WhileExpr::infer(TypeChecker& checker) const {
    checker.check(*cond, checker.prim_type(artic::PrimType::Bool));
    checker.check(*body, checker.unit_type());
    return checker.unit_type();
}

artic::Type BreakExpr::infer(TypeChecker& checker) const {
    return checker.fn_type(checker.unit_type(), checker.no_ret_type());
}

artic::Type ContinueExpr::infer(TypeChecker& checker) const {
    return checker.fn_type(checker.unit_type(), checker.no_ret_type());
}

artic::Type ReturnExpr::infer(TypeChecker& checker) const {
    // This error has been reported by the NameBinder already
    if (!fn || !fn->type.isa<artic::FnType>())
        return checker.error_type();
    auto fn_type = fn->type.as<artic::FnType>();
    return checker.fn_type(fn_type.from(), checker.no_ret_type());
}

// Declarations --------------------------------------------------------------------

artic::Type PtrnDecl::check(TypeChecker&, artic::Type expected) const {
    return expected;
}

artic::Type LetDecl::infer(TypeChecker& checker) const {
    checker.check(*ptrn, checker.infer(*init));
    return checker.unit_type();
}

artic::Type FnDecl::infer(TypeChecker& checker) const {
    // TODO: Type params
    return checker.infer(*fn);
}

// Patterns ------------------------------------------------------------------------

artic::Type TypedPtrn::infer(TypeChecker& checker) const {
    return checker.check(*ptrn, checker.infer(*type));
}

artic::Type IdPtrn::check(TypeChecker& checker, artic::Type expected) const {
    return checker.check(*decl, expected);
}

artic::Type TuplePtrn::infer(TypeChecker& checker) const {
    return checker.infer_tuple(args);
}

artic::Type TuplePtrn::check(TypeChecker& checker, artic::Type expected) const {
    return checker.check_tuple(loc, "tuple pattern", args, expected);
}

artic::Type Program::infer(TypeChecker& checker) const {
    for (auto& decl : decls)
        checker.infer(*decl);
    // TODO: Return proper type for the module
    return artic::Type();
}

} // namespace ast

} // namespace artic
