#include <typeinfo>
#include <algorithm>

#include "types.h"
#include "hash.h"

namespace artic {

// Types ---------------------------------------------------------------------------

bool PrimType::equals(const Type* other) const {
    return other->isa<PrimType>() && other->as<PrimType>()->tag == tag;
}

size_t PrimType::hash() const {
    return fnv::Hash().combine(typeid(*this).hash_code()).combine(tag);
}

bool TupleType::equals(const Type* other) const {
    return other->isa<TupleType>() && other->as<TupleType>()->args == args;
}

size_t TupleType::hash() const {
    auto h = fnv::Hash().combine(typeid(*this).hash_code());
    for (auto a : args)
        h.combine(a);
    return h;
}

bool TupleType::contains(const Type* type) const {
    return
        type == this ||
        std::any_of(args.begin(), args.end(), [type] (auto a) {
            return a->contains(type);
        });
}

const Type* TupleType::replace(
    TypeTable& type_table,
    const std::unordered_map<const TypeVar*, const Type*>& map) const {
    std::vector<const Type*> new_args(args.size());
    for (size_t i = 0, n = args.size(); i < n; ++i)
        new_args[i] = args[i]->replace(type_table, map);
    return type_table.tuple_type(std::move(new_args));
}

bool ArrayType::contains(const Type* type) const {
    return type == this || elem->contains(type);
}

bool SizedArrayType::equals(const Type* other) const {
    return
        other->isa<SizedArrayType>() &&
        other->as<SizedArrayType>()->elem == elem &&
        other->as<SizedArrayType>()->size == size;
}

size_t SizedArrayType::hash() const {
    return fnv::Hash()
        .combine(typeid(*this).hash_code())
        .combine(elem)
        .combine(size);
}

const Type* SizedArrayType::replace(
    TypeTable& type_table,
    const std::unordered_map<const TypeVar*, const Type*>& map) const {
    return type_table.sized_array_type(elem->replace(type_table, map), size);
}

bool UnsizedArrayType::equals(const Type* other) const {
    return
        other->isa<UnsizedArrayType>() &&
        other->as<UnsizedArrayType>()->elem == elem;
}

size_t UnsizedArrayType::hash() const {
    return fnv::Hash()
        .combine(typeid(*this).hash_code())
        .combine(elem);
}

const Type* UnsizedArrayType::replace(
    TypeTable& type_table,
    const std::unordered_map<const TypeVar*, const Type*>& map) const {
    return type_table.unsized_array_type(elem->replace(type_table, map));
}

bool PtrType::equals(const Type* other) const {
    return other->isa<PtrType>() && other->as<PtrType>()->pointee == pointee;
}

size_t PtrType::hash() const {
    return fnv::Hash()
        .combine(typeid(*this).hash_code())
        .combine(pointee);
}

bool PtrType::contains(const Type* type) const {
    return type == this || pointee->contains(type);
}

const Type* PtrType::replace(
    TypeTable& type_table,
    const std::unordered_map<const TypeVar*, const Type*>& map) const {
    return type_table.ptr_type(pointee->replace(type_table, map));
}

bool FnType::equals(const Type* other) const {
    return
        other->isa<FnType>() &&
        other->as<FnType>()->dom == dom &&
        other->as<FnType>()->codom == codom;
}

size_t FnType::hash() const {
    return fnv::Hash()
        .combine(typeid(*this).hash_code())
        .combine(dom)
        .combine(codom);
}

bool FnType::contains(const Type* type) const {
    return type == this || dom->contains(type) || codom->contains(type);
}

const Type* FnType::replace(
    TypeTable& type_table,
    const std::unordered_map<const TypeVar*, const Type*>& map) const {
    return type_table.fn_type(dom->replace(type_table, map), codom->replace(type_table, map));
}

bool NoRetType::equals(const Type* other) const {
    return other->isa<NoRetType>();
}

size_t NoRetType::hash() const {
    return fnv::Hash().combine(typeid(*this).hash_code());
}

bool TypeError::equals(const Type* other) const {
    return other->isa<TypeError>();
}

size_t TypeError::hash() const {
    return fnv::Hash().combine(typeid(*this).hash_code());
}

bool TypeVar::equals(const Type* other) const {
    return other == this;
}

size_t TypeVar::hash() const {
    return fnv::Hash().combine(&param);
}

const Type* TypeVar::replace(
    TypeTable&,
    const std::unordered_map<const TypeVar*, const Type*>& map) const {
    if (auto it = map.find(this); it != map.end())
        return it->second;
    return this;
}

const Type* ForallType::instantiate(TypeTable& type_table, const std::vector<const Type*>& args) const {
    std::unordered_map<const TypeVar*, const Type*> map;
    assert(decl.type_params && decl.type_params->params.size() == args.size());
    for (size_t i = 0, n = args.size(); i < n; ++i) {
        assert(decl.type_params->params[i]->type);
        map.emplace(decl.type_params->params[i]->type->as<TypeVar>(), args[i]); 
    }
    return body->replace(type_table, map);
}

bool ForallType::equals(const Type* other) const {
    return other == this;
}

size_t ForallType::hash() const {
    return fnv::Hash().combine(&decl);
}

bool StructType::equals(const Type* other) const {
    return other == this;
}

size_t StructType::hash() const {
    return fnv::Hash().combine(&decl);
}

const ast::TypeParamList* StructType::type_params() const {
    return decl.type_params.get();
}

std::optional<size_t> StructType::find_member(const std::string_view& name) const {
    auto it = std::find_if(
        decl.fields.begin(),
        decl.fields.end(),
        [&name] (auto& f) {
            return f->id.name == name;
        });
    return it != decl.fields.end()
        ? std::make_optional(it - decl.fields.begin())
        : std::nullopt;
}

const Type* StructType::member_type(size_t i) const {
    return decl.fields[i]->ast::Node::type;
}

size_t StructType::member_count() const {
    return decl.fields.size();
}

bool EnumType::equals(const Type* other) const {
    return other == this;
}

size_t EnumType::hash() const {
    return fnv::Hash().combine(&decl);
}

const ast::TypeParamList* EnumType::type_params() const {
    return decl.type_params.get();
}

std::optional<size_t> EnumType::find_member(const std::string_view& name) const {
    auto it = std::find_if(
        decl.options.begin(),
        decl.options.end(),
        [&name] (auto& o) {
            return o->id.name == name;
        });
    return it != decl.options.end()
        ? std::make_optional(it - decl.options.begin())
        : std::nullopt;
}

const Type* EnumType::member_type(size_t i) const {
    return decl.options[i]->type;
}

size_t EnumType::member_count() const {
    return decl.options.size();
}

const Type* TypeApp::member_type(TypeTable& type_table, size_t i) const {
    std::unordered_map<const TypeVar*, const Type*> map;
    auto type_params = applied->type_params();
    assert(type_params && type_params->params.size() == type_args.size());
    for (size_t i = 0, n = type_args.size(); i < n; ++i) {
        assert(type_params->params[i]->type);
        map.emplace(type_params->params[i]->type->as<TypeVar>(), type_args[i]); 
    }
    return applied->member_type(i)->replace(type_table, map);
}

bool TypeApp::equals(const Type* other) const {
    return
        other->isa<TypeApp>() &&
        other->as<TypeApp>()->applied == applied &&
        other->as<TypeApp>()->type_args == type_args;
}

size_t TypeApp::hash() const {
    auto h = fnv::Hash().combine(typeid(*this).hash_code()).combine(applied);
    for (auto a : type_args)
        h.combine(a);
    return h;
}

bool TypeApp::contains(const Type* type) const {
    return
        type == this ||
        applied->contains(type) ||
        std::any_of(type_args.begin(), type_args.end(), [type] (auto a) {
            return a->contains(type);
        });
}

const Type* TypeApp::replace(
    TypeTable& type_table,
    const std::unordered_map<const TypeVar*, const Type*>& map) const {
    std::vector<const Type*> new_type_args(type_args.size());
    for (size_t i = 0, n = type_args.size(); i < n; ++i)
        new_type_args[i] = type_args[i]->replace(type_table, map);
    return type_table.type_app(applied, std::move(new_type_args));
}

// Helpers -------------------------------------------------------------------------

bool is_int_type(const Type* type) {
    if (auto prim_type = type->isa<PrimType>()) {
        switch (prim_type->tag) {
            case ast::PrimType::U8:
            case ast::PrimType::U16:
            case ast::PrimType::U32:
            case ast::PrimType::U64:
            case ast::PrimType::I8:
            case ast::PrimType::I16:
            case ast::PrimType::I32:
            case ast::PrimType::I64:
                return true; 
            default:
                break;
        }
    }
    return false;
}

bool is_float_type(const Type* type) {
    if (auto prim_type = type->isa<PrimType>()) {
        switch (prim_type->tag) {
            case ast::PrimType::F32:
            case ast::PrimType::F64:
                return true; 
            default:
                break;
        }
    }
    return false;
}

bool is_int_or_float_type(const Type* type) {
    return is_int_type(type) || is_float_type(type);
}

bool is_bool_type(const Type* type) {
    return type->isa<PrimType>() && type->as<PrimType>()->tag == ast::PrimType::Bool;
}

bool is_unit_type(const Type* type) {
    return type->isa<TupleType>() && type->as<TupleType>()->args.empty();
}

const Type* join_types(const Type* left, const Type* right) {
    // Returns the join of two types according to the subtyping relation
    if (left == right)
        return left;
    if (left->isa<NoRetType>())
        return right;
    if (right->isa<NoRetType>())
        return left;
    if (auto [l, r] = std::make_pair(left->isa<SizedArrayType>(), right->isa<UnsizedArrayType>()); l && r) {
        if (l->elem == r->elem)
            return r;
    }
    if (auto [l, r] = std::make_pair(left->isa<UnsizedArrayType>(), right->isa<SizedArrayType>()); l && r) {
        if (l->elem == r->elem)
            return l;
    }
    return nullptr;
}

// Type table ----------------------------------------------------------------------

TypeTable::~TypeTable() {
    for (auto t : types_)
        delete t;
}

const PrimType* TypeTable::prim_type(ast::PrimType::Tag tag) {
    return insert<PrimType>(tag);
}

const PrimType* TypeTable::bool_type() {
    return prim_type(ast::PrimType::Bool);
}

const TupleType* TypeTable::unit_type() {
    return unit_type_ ? unit_type_ : unit_type_ = tuple_type({});
}

const TupleType* TypeTable::tuple_type(std::vector<const Type*>&& elems) {
    return insert<TupleType>(std::move(elems));
}

const SizedArrayType* TypeTable::sized_array_type(const Type* elem, size_t size) {
    return insert<SizedArrayType>(elem, size);
}
 
const UnsizedArrayType* TypeTable::unsized_array_type(const Type* elem) {
    return insert<UnsizedArrayType>(elem);
}

const PtrType* TypeTable::ptr_type(const Type* pointee) {
    return insert<PtrType>(pointee);
}

const FnType* TypeTable::fn_type(const Type* dom, const Type* codom) {
    return insert<FnType>(dom, codom);
}

const FnType* TypeTable::cn_type(const Type* dom) {
    return fn_type(dom, no_ret_type());
}

const NoRetType* TypeTable::no_ret_type() {
    return no_ret_type_ ? no_ret_type_ : no_ret_type_ = insert<NoRetType>();
}

const TypeError* TypeTable::type_error() {
    return type_error_ ? type_error_ : type_error_ = insert<TypeError>();
}

const TypeVar* TypeTable::type_var(const ast::TypeParam& param) {
    return insert<TypeVar>(param);
}

const ForallType* TypeTable::forall_type(const ast::FnDecl& decl) {
    return insert<ForallType>(decl);
}

const StructType* TypeTable::struct_type(const ast::StructDecl& decl) {
    return insert<StructType>(decl);
}

const EnumType* TypeTable::enum_type(const ast::EnumDecl& decl) {
    return insert<EnumType>(decl);
}

const TypeApp* TypeTable::type_app(const UserType* applied, std::vector<const Type*>&& type_args) {
    return insert<TypeApp>(applied, std::move(type_args));
}

template <typename T, typename... Args>
const T* TypeTable::insert(Args&&... args) {
    T t(std::forward<Args>(args)...);
    if (auto it = types_.find(&t); it != types_.end())
        return (*it)->template as<T>();
    auto [it, _] = types_.emplace(new T(std::move(t)));
    return (*it)->template as<T>();
}

} // namespace artic
