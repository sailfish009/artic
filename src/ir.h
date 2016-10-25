#ifndef IR_H
#define IR_H

#include <cstdint>
#include <vector>
#include <string>

#include "cast.h"
#include "types.h"
#include "loc.h"

namespace artic {

class IRBuilder;
class PrettyPrinter;
class CheckSema;
class InferSema;

/// The IR follows an ANF structure. Its grammar is the following:
///
///     EXPR = CEXPR | Let [(id, type, CEXPR)] EXPR
///
/// Complex expressions:
///     CEXPR = IFEXPR | APPEXPR | AEXPR
///     IFEXPR = If VALUE EXPR EXPR
///     APPEXPR = App [VALUE]
///
/// Atomic expressions:
///     AEXPR = VALUE | PRIMOP
///     PRIMOP = PrimOp OP [VALUE]
///     OP = +, -, *, /, CMP, ...
///     VALUE = VECTOR | TUPLE | VAR | PARAM | LAMBDA
///     VECTOR = SCALAR | Vec SCALAR VECTOR
///     SCALAR = I1 <bool> | I8 <int> | I16 <int> | ...
///     TUPLE = Tuple [VALUE]
///     VAR = Var <string>
///     LAMBDA = Lambda <string> EXPR
///
/// Types:
///     TYPE = PRIMTYPE | TUPLETYPE | LAMBDATYPE
///     VECTORTYPE = SCALARTYPE | VecType SCALARTYPE VECTORTYPE
///     SCALARTYPE = TypeI1 | TypeI8 | TypeI16 | ...
///     TUPLETYPE = TupleType [TYPE]
///     LAMBDATYPE = Lambda TYPE TYPE

/// Base class for expressions.
class Expr : public Cast<Expr> {
    friend class IRBuilder;
    friend class CheckSema;
    friend class InferSema;
    friend class Parser;

public:
    virtual ~Expr() {}

    /// Returns the type of the expression (after type-checking).
    const Type* type() const { return type_; }

    /// Returns the location of the expression in the file.
    const Loc& loc() const { return loc_; }
    void set_loc(const Loc& l) { loc_ = l; }

    /// Returns the builder that was used to create this node.
    IRBuilder* builder() const { return builder_; }

    /// Computes the complexity of the expression (used for pretty printing).
    virtual size_t complexity() const { return 1; }

    /// Prints the expression in a human-readable form.
    virtual void print(PrettyPrinter&) const = 0;

    /// Dumps the expression without any indentation nor coloring.
    void dump() const;

    /// Type checks an expression.
    virtual void check(CheckSema&) const = 0;
    /// Infers the type of the expression.
    virtual const Type* infer(InferSema&) const = 0;

protected:
    Expr() : type_(nullptr), builder_(nullptr) {}

    void assign_type(const Type* type) const { type_ = type; }

private:
    IRBuilder* builder_;
    mutable const Type* type_;
    Loc loc_;
};

/// Base class for complex expressions. Complex expressions are expressions that may not terminate.
class ComplexExpr : public Expr {
public:
    virtual ~ComplexExpr() {}
};

/// Base class for atomic expressions. Atomic expressions are expressions that are guaranteed to terminate.
class AtomicExpr : public ComplexExpr {
public:
    virtual ~AtomicExpr() {}
};

/// Base class for values.
class Value : public AtomicExpr {
public:
    virtual ~Value() {}
};

/// Scalar value or vector that holds elements of the same type.
class Vector : public Value {
    friend class IRBuilder;

    Vector() {}
    template <typename... Args>
    Vector(Args... args) { set(args...); }

public:
    union Elem {
        Elem() {}
        bool     i1 ; Elem(bool     i1 ) : i1 (i1 ) {};
        int8_t   i8 ; Elem(int8_t   i8 ) : i8 (i8 ) {};
        int16_t  i16; Elem(int16_t  i16) : i16(i16) {};
        int32_t  i32; Elem(int32_t  i32) : i32(i32) {};
        int64_t  i64; Elem(int64_t  i64) : i64(i64) {};
        uint8_t  u8 ; Elem(uint8_t  u8 ) : u8 (u8 ) {};
        uint16_t u16; Elem(uint16_t u16) : u16(u16) {};
        uint32_t u32; Elem(uint32_t u32) : u32(u32) {};
        uint64_t u64; Elem(uint64_t u64) : u64(u64) {};
        float    f32; Elem(float    f32) : f32(f32) {};
        double   f64; Elem(double   f64) : f64(f64) {};
    };

    template <typename... Args>
    void set(Prim p, Args... args) {
        prim_ = p;
        elems_.resize(sizeof...(args));
        set_<0>(args...);
    }

    template <typename T, typename... Args>
    void set(T t, Args... args) {
        // Infer the type based on the first parameter
        prim_ = RepToPrim<T>::prim();
        elems_.resize(sizeof...(args) + 1);
        set_<0>(t, args...);
    }

    const std::vector<Elem>& elems() const { return elems_; }
    std::vector<Elem>& elems() { return elems_; }

    Elem elem(int i) const { return elems_[i]; }
    Elem value() const { return elems_[0]; }

    void set_elem(int i, Elem e) { elems_[i] = e; }
    void set_value(Elem e) { elems_[0] = e; }
    void set_prim(Prim p) { prim_ = p; }

    Prim prim() const { return prim_; }
    size_t size() const { return elems_.size(); }
    void resize(size_t s) { elems_.resize(s); }

    bool is_integer() const { return artic::is_integer(prim()); }
    int bit_count() const { return artic::bitcount(prim()) * size(); }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    template <int K, typename T, typename... Args>
    void set_(T t, Args... args) {
        elems_[K] = Elem(t);
        set_<K + 1>(args...);
    }

    template <int K>
    void set_() {}

    Prim prim_;
    std::vector<Elem> elems_;
};

/// Tuple value that holds several values of (possibly) different types.
class Tuple : public Value {
    friend class IRBuilder;

    Tuple(const std::vector<const Value*>& v = std::vector<const Value*>()) : elems_(v) {}

public:
    const std::vector<const Value*>& elems() const { return elems_; }
    std::vector<const Value*>& elems() { return elems_; }

    const Value* elem(int i) const { return elems_[i]; }
    void set_elem(int i, const Value* v) { elems_[i] = v; }
    size_t size() const { return elems_.size(); }

    size_t complexity() const override {
        size_t c = 1;
        for (auto e : elems()) c += e->complexity();
        return c;
    }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    std::vector<const Value*> elems_;
};

/// Variable binding coming from a let expression.
class Var : public Value {
    friend class IRBuilder;

    Var(const std::string& n, const ComplexExpr* b)
        : name_(n), binding_(b)
    {}

public:
    const ComplexExpr* binding() const { return binding_; }
    void set_binding(const ComplexExpr* b) { binding_ = b; }

    const std::string& name() const { return name_; }
    void set_name(const std::string& n) { name_ = n; }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    const ComplexExpr* binding_;
    std::string name_;
};

/// Parameter coming from a lambda expression.
class Param : public Value {
    friend class IRBuilder;

    Param(const std::string& n)
        : name_(n)
    {}

public:
    const std::string& name() const { return name_; }
    void set_name(const std::string& n) { name_ = n; }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    std::string name_;
};

/// Lambda function abstraction, composed of the argument with its type, and the corresponding body.
class Lambda : public Value {
    friend class IRBuilder;

    Lambda(const Param* p, const Expr* b = nullptr)
        : param_(p), body_(b)
    {}

public:
    const Param* param() const { return param_; }
    void set_param(const Param* p) { param_ = p; }

    const Expr* body() const { return body_; }
    void set_body(const Expr* e) { body_ = e; }

    size_t complexity() const override { return 1 + body_->complexity(); }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    const Param* param_;
    const Expr* body_;
};

/// Primitive operation on values.
class PrimOp : public AtomicExpr {
    friend class IRBuilder;

public:
    enum Op {
        ADD, SUB, MUL, DIV,                         // Arithmetic
        RSHFT, LSHFT, AND, OR, XOR,                 // Bitwise
        CMP_GE, CMP_LE, CMP_GT, CMP_LT, CMP_EQ,     // Comparison
        SELECT, BITCAST, ELEM                       // Misc.
    };

private:
    PrimOp(Op op, const Type* t, const Value* a)
        : op_(op) {
        assert(op == BITCAST);
        args_.push_back(a);
        type_args_.push_back(t);
    }

    PrimOp(Op op, const Value* a, const Value* b)
        : op_(op) {
        args_.push_back(a);
        args_.push_back(b);
    }

    PrimOp(Op op, const Value* a, const Value* b, const Value* c)
        : op_(op) {
        assert(op == SELECT);
        args_.push_back(a);
        args_.push_back(b);
        args_.push_back(c);
    }

public:
    /// Evaluates the result of the operation.
    const Value* eval() const;

    Op op() const { return op_; }

    const std::vector<const Type*>& type_args() const { return type_args_; }
    std::vector<const Type*>& type_args() { return type_args_; }
    const Type* type_arg(int i = 0) const { return type_args_[i]; }
    size_t num_type_args() const { return type_args_.size(); }

    const std::vector<const Value*>& args() const { return args_; }
    std::vector<const Value*>& args() { return args_; }
    const Value* arg(int i = 0) const { return args_[i]; }
    size_t num_args() const { return args_.size(); }

    bool binary() const { return op() <= CMP_EQ; }

    size_t complexity() const override {
        size_t c = 1 + num_type_args();
        for (auto a : args()) c += a->complexity();
        return c;
    }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    Op op_;
    std::vector<const Value*> args_;
    std::vector<const Type*> type_args_;
};

/// If-expression, which evaluates either one of its branches based on some condition.
class IfExpr : public ComplexExpr {
    friend class IRBuilder;

    IfExpr(const Value* cond, const Expr* if_true, const Expr* if_false)
        : cond_(cond), if_true_(if_true), if_false_(if_false)
    {}

public:
    const Value* cond() const { return cond_; }
    const Expr* if_true() const { return if_true_; }
    const Expr* if_false() const { return if_false_; }

    void set_cond(const Value* v) { cond_ = v; }
    void set_if_true(const Expr* e) { if_true_ = e; }
    void set_if_false(const Expr* e) { if_false_ = e; }

    size_t complexity() const override {
        return 1 +
               cond()->complexity() +
               if_true()->complexity() +
               if_false()->complexity();
    }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    const Value* cond_;
    const Expr* if_true_, *if_false_;
};

/// Lambda application expression.
class AppExpr : public ComplexExpr {
    friend class IRBuilder;

    AppExpr(const std::vector<const Value*>& args)
        : args_(args)
    {}

public:
    const std::vector<const Value*>& args() const { return args_; }
    std::vector<const Value*>& args() { return args_; }
    const Value* arg(int i = 0) const { return args_[i]; }
    size_t num_args() const { return args_.size(); }

    size_t complexity() const override {
        size_t c = 1;
        for (auto a : args()) c += a->complexity();
        return c;
    }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    std::vector<const Value*> args_;
};

/// Let-expression, introducing a new variable in the scope of an expression.
class LetExpr : public Expr {
    friend class IRBuilder;

    LetExpr(const Var* var, const Expr* e = nullptr)
        : var_(var), body_(e)
    {}

public:
    const Var* var() const { return var_; }
    void set_var(const Var* var) { var_ = var; }

    const Expr* body() const { return body_; }
    void set_body(const Expr* e) { body_ = e; }

    size_t complexity() const override {
        return 1 + var()->binding()->complexity() + body()->complexity();
    }

    void print(PrettyPrinter&) const override;
    void check(CheckSema&) const override;
    const Type* infer(InferSema&) const override;

private:
    const Var* var_;
    const Expr* body_;
};

} // namespace artic

#endif // IR_H
