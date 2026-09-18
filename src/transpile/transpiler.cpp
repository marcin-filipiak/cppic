#include "transpile/transpiler.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace cppic {
namespace {

bool isPointerLike(const TypePtr& t) {
    return t && (t->kind == Type::Kind::Pointer || t->kind == Type::Kind::Reference);
}

std::string binSym(BinOp o) {
    switch (o) {
        case BinOp::Mul: return "*";
        case BinOp::Div: return "/";
        case BinOp::Mod: return "%";
        case BinOp::Add: return "+";
        case BinOp::Sub: return "-";
        case BinOp::Shl: return "<<";
        case BinOp::Shr: return ">>";
        case BinOp::Lt: return "<";
        case BinOp::Gt: return ">";
        case BinOp::Le: return "<=";
        case BinOp::Ge: return ">=";
        case BinOp::Eq: return "==";
        case BinOp::Ne: return "!=";
        case BinOp::BitAnd: return "&";
        case BinOp::BitXor: return "^";
        case BinOp::BitOr: return "|";
        case BinOp::LAnd: return "&&";
        case BinOp::LOr: return "||";
    }
    return "?";
}

std::string escapeCString(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        switch (c) {
            case '\n': r += "\\n"; break;
            case '\t': r += "\\t"; break;
            case '\r': r += "\\r"; break;
            case '\\': r += "\\\\"; break;
            case '"': r += "\\\""; break;
            default:
                if (c < 32 || c > 126) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\%03o", c);
                    r += buf;
                } else {
                    r += static_cast<char>(c);
                }
        }
    }
    return r;
}

}  // namespace

std::string Transpiler::indentStr(int n) const { return std::string(n * 4, ' '); }

void Transpiler::err(const std::string& msg, SourceLoc loc) {
    throw TranspileError{msg, loc};
}

// ---------------------------------------------------------------------------
// Name mangling
// ---------------------------------------------------------------------------

std::string Transpiler::mangleName(const std::string& n) {
    std::string r;
    for (std::size_t i = 0; i < n.size(); ++i) {
        if (n[i] == ':' && i + 1 < n.size() && n[i + 1] == ':') {
            r += "__";
            ++i;
        } else if (std::isalnum(static_cast<unsigned char>(n[i])) || n[i] == '_') {
            r += n[i];
        } else {
            r += '_';
        }
    }
    return r;
}

std::string Transpiler::mangleOperator(const std::string& name) {
    std::string op = name;
    if (op.rfind("operator", 0) == 0) op = op.substr(8);
    struct Entry { const char* sym; const char* code; };
    static const Entry table[] = {
        {" new", "new"}, {" delete", "delete"}, {"()", "call"}, {"[]", "index"},
        {"<<=", "shl_assign"}, {">>=", "shr_assign"},
        {"+=", "add_assign"}, {"-=", "sub_assign"}, {"*=", "mul_assign"},
        {"/=", "div_assign"}, {"%=", "mod_assign"}, {"&=", "and_assign"},
        {"|=", "or_assign"}, {"^=", "xor_assign"},
        {"++", "inc"}, {"--", "dec"},
        {"==", "eq"}, {"!=", "ne"}, {"<=", "le"}, {">=", "ge"},
        {"&&", "land"}, {"||", "lor"},
        {"<<", "shl"}, {">>", "shr"},
        {"+", "add"}, {"-", "sub"}, {"*", "mul"}, {"/", "div"}, {"%", "mod"},
        {"&", "and_"}, {"|", "or_"}, {"^", "xor_"}, {"~", "bitnot"}, {"!", "not"},
        {"=", "assign"}, {"<", "lt"}, {">", "gt"},
    };
    for (const auto& e : table)
        if (op == e.sym) return e.code;
    return "op";
}

std::string Transpiler::classCName(const std::string& name) const {
    auto it = classes_.find(name);
    if (it != classes_.end()) return it->second.cname;
    return mangleName(name);
}

std::string Transpiler::mangleFunction(const FunctionDecl& f, const ClassInfo* ci) {
    if (!ci) return mangleName(f.name);
    std::string method = f.name;
    std::size_t pos = method.rfind("::");
    if (pos != std::string::npos) method = method.substr(pos + 2);
    if (f.isConstructor) return ci->cname + "__ctor";
    if (f.isDestructor) return ci->cname + "__dtor";
    if (method.rfind("operator", 0) == 0)
        return ci->cname + "__" + mangleOperator(method);
    return ci->cname + "__" + method;
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

std::string Transpiler::typeToC(const Type& t) const {
    switch (t.kind) {
        case Type::Kind::Builtin: {
            const auto& b = std::get<Type::BuiltinData>(t.data);
            std::string q;
            if (b.isConst) q += " const";
            if (b.isVolatile) q += " volatile";
            if (b.base == Type::Bases_Void) return "void" + q;
            if (b.base == Type::Bases_Bool) return "unsigned char" + q;
            if (b.base == Type::Bases_Float) return "float" + q;
            if (b.base == Type::Bases_Double) return "float" + q;  // PIC18 has no double
            std::string s;
            if (b.isUnsigned) s += "unsigned ";
            if (b.base == Type::Bases_Char) {
                s += "char";
            } else if (b.isShort) {
                s += "short";
            } else if (b.isLong) {
                s += "long";
            } else {
                s += "int";
            }
            return s + q;
        }
        case Type::Kind::Pointer: {
            const auto& pd = std::get<Type::PointerData>(t.data);
            return typeToC(*pd.pointee) + "*" + (pd.isConst ? " const" : "");
        }
        case Type::Kind::Reference:
            return typeToC(*std::get<Type::ReferenceData>(t.data).referent) + "*";
        case Type::Kind::Array: {
            const auto& a = std::get<Type::ArrayData>(t.data);
            return typeToC(*a.element);
        }
        case Type::Kind::Named: {
            const std::string& n = std::get<Type::NamedData>(t.data).name;
            return n == "String" ? "CppicString" : classCName(n);
        }
        case Type::Kind::Function:
            return "void*";
    }
    return "int";
}

bool Transpiler::isClassType(const TypePtr& t) const {
    if (!t || t->kind != Type::Kind::Named) return false;
    return classes_.count(std::get<Type::NamedData>(t->data).name) > 0;
}

bool Transpiler::isStringType(const TypePtr& t) const {
    return t && t->kind == Type::Kind::Named &&
           std::get<Type::NamedData>(t->data).name == "String";
}

const Transpiler::ClassInfo* Transpiler::classOf(const TypePtr& t) const {
    if (!isClassType(t)) return nullptr;
    auto it = classes_.find(std::get<Type::NamedData>(t->data).name);
    return it == classes_.end() ? nullptr : &it->second;
}

const Transpiler::ClassInfo* Transpiler::classOfPointee(const TypePtr& t) const {
    TypePtr d = stripRef(t);
    if (d && d->kind == Type::Kind::Pointer)
        d = std::get<Type::PointerData>(d->data).pointee;
    if (d && d->kind == Type::Kind::Reference)
        d = std::get<Type::ReferenceData>(d->data).referent;
    return classOf(d);
}

TypePtr Transpiler::stripRef(const TypePtr& t) const {
    if (t && t->kind == Type::Kind::Reference)
        return std::get<Type::ReferenceData>(t->data).referent;
    return t;
}

TypePtr Transpiler::derefType(const TypePtr& t) const {
    if (!t) return nullptr;
    if (t->kind == Type::Kind::Pointer)
        return std::get<Type::PointerData>(t->data).pointee;
    if (t->kind == Type::Kind::Reference)
        return std::get<Type::ReferenceData>(t->data).referent;
    return nullptr;
}

// ---------------------------------------------------------------------------
// String lowering
// ---------------------------------------------------------------------------

// Emits C for `dst = rhs` where dst is "&s" (or a String pointer) and rhs is
// an expression assignable to an Arduino-style String.
std::string Transpiler::stringAssignCall(const std::string& dst, const Expr& rhs) {
    if (rhs.kind == Expr::Kind::StrLit)
        return "cppic_string_set(" + dst + ", \"" + escapeCString(rhs.stringValue) + "\")";
    if (rhs.kind == Expr::Kind::Binary && rhs.binOp == BinOp::Add) {
        Val lv = emitExpr(*rhs.lhs);
        Val rv = emitExpr(*rhs.rhs);
        bool lS = isStringType(stripRef(lv.type));
        bool rS = isStringType(stripRef(rv.type));
        std::string ar = lv.ptr ? lv.code : "&" + lv.code;
        std::string br = rv.ptr ? rv.code : "&" + rv.code;
        if (lS && rS)
            return "cppic_string_concat(" + dst + ", " + ar + ", " + br + ")";
        if (lS && !rS)
            return "cppic_string_concat_lit(" + dst + ", " + ar + ", " + rv.code + ")";
        if (!lS && rS)
            return "cppic_string_concat_llit(" + dst + ", " + lv.code + ", " + br + ")";
        err("unsupported String concatenation operands", rhs.loc);
    }
    Val v = emitExpr(rhs);
    if (isStringType(stripRef(v.type))) {
        std::string src = v.ptr ? v.code : "&" + v.code;
        return "cppic_string_copy(" + dst + ", " + src + ")";
    }
    err("cannot assign this expression to String", rhs.loc);
}

// Emits C for `dst += rhs`.
std::string Transpiler::stringAppendCall(const std::string& dst, const Expr& rhs) {
    if (rhs.kind == Expr::Kind::StrLit)
        return "cppic_string_append_lit(" + dst + ", \"" +
               escapeCString(rhs.stringValue) + "\")";
    Val v = emitExpr(rhs);
    if (isStringType(stripRef(v.type))) {
        std::string src = v.ptr ? v.code : "&" + v.code;
        return "cppic_string_append(" + dst + ", " + src + ")";
    }
    err("unsupported String += operand (append String value, literals or "
        "split concatenations into statements)",
        rhs.loc);
}

std::string Transpiler::stringFunctionName(const std::string& member) {
    static const std::unordered_map<std::string, std::string> m = {
        {"length", "cppic_string_length"},
        {"charAt", "cppic_string_char_at"},
        {"c_str", "cppic_string_c_str"},
        {"isEmpty", "cppic_string_is_empty"},
        {"startsWith", "cppic_string_starts_with"},
        {"endsWith", "cppic_string_ends_with"},
        {"indexOf", "cppic_string_index_of_char"},
    };
    auto it = m.find(member);
    return it == m.end() ? "" : it->second;
}

// ---------------------------------------------------------------------------
// Collection
// ---------------------------------------------------------------------------

void Transpiler::collect(const TranslationUnit& tu) {
    // 1st pass: classes
    for (const auto& d : tu.decls) {
        if (d->kind == Decl::Kind::Class) collectClass(d->klass);
    }
    // resolve inheritance / flatten fields
    for (auto& [name, ci] : classes_) addInstanceFields(ci);

    // 2nd pass: typedefs, enums, globals, free functions
    for (const auto& d : tu.decls) {
        switch (d->kind) {
            case Decl::Kind::Typedef:
                typedefs_[d->typedefName] = d->typedefType;
                break;
            case Decl::Kind::Enum:
                for (const auto& e : d->enumm->enumerators) {
                    if (e.value && e.value->kind == Expr::Kind::IntLit)
                        enumConsts_[e.name] =
                            static_cast<long long>(e.value->intValue);
                }
                break;
            case Decl::Kind::Var:
                globals_[d->var->name] = d->var.get();
                break;
            case Decl::Kind::Function:
                if (d->func->name.find("::") == std::string::npos)
                    freeFuncs_[d->func->name].push_back(d->func.get());
                break;
            default:
                break;
        }
    }
}

void Transpiler::collectClass(const std::shared_ptr<ClassDecl>& c) {
    if (!c || c->name.empty()) return;
    ClassInfo ci;
    ci.name = c->name;
    ci.cname = mangleName(c->name);
    ci.isStruct = c->isStruct;
    ci.isUnion = c->isUnion;
    ci.bases = c->bases;

    for (const auto& m : c->members) {
        if (m.kind == ClassMember::Kind::Field) {
            if (m.field->isStatic)
                ci.staticFields.push_back(m.field.get());
            else
                ci.instanceFields.push_back(m.field.get());
        } else if (m.kind == ClassMember::Kind::Method) {
            const FunctionDecl* f = m.method.get();
            if (f->isConstructor) {
                ci.hasUserCtor = true;
                if (f->params.empty()) ci.hasDefaultCtor = true;
                ci.ctorOverloads.push_back(f);
            } else if (f->isDestructor) {
                ci.dtor = f;
            } else {
                ci.methods[f->name].push_back(f);
            }
        }
    }
    classes_[ci.name] = std::move(ci);
}

void Transpiler::addInstanceFields(ClassInfo& ci) {
    if (!ci.instanceFields.empty() || ci.bases.empty()) {
        for (auto* f : ci.instanceFields) ci.fieldByName[f->name] = f;
        for (auto& [k, v] : ci.fieldByName) (void)k, (void)v;
        return;
    }
    std::vector<const VarDecl*> all;
    for (const auto& b : ci.bases) {
        auto it = classes_.find(b);
        if (it != classes_.end()) {
            addInstanceFields(it->second);
            for (auto* f : it->second.instanceFields) all.push_back(f);
        }
    }
    for (auto* f : ci.instanceFields) all.push_back(f);
    for (auto* f : all) ci.fieldByName[f->name] = f;
}

// ---------------------------------------------------------------------------
// Scope
// ---------------------------------------------------------------------------

void Transpiler::pushScope() { scopes_.emplace_back(); }
void Transpiler::popScope() { scopes_.pop_back(); }

void Transpiler::declareVar(const std::string& name, TypePtr t) {
    if (scopes_.empty()) pushScope();
    scopes_.back().vars[name] = std::move(t);
}

TypePtr Transpiler::lookupVar(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->vars.find(name);
        if (f != it->vars.end()) return f->second;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

Transpiler::Val Transpiler::emitExpr(const Expr& e) {
    switch (e.kind) {
        case Expr::Kind::IntLit:
            return {std::to_string(e.intValue), Type::builtin(Type::Bases_Int), false};
        case Expr::Kind::FloatLit: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", e.floatValue);
            return {buf, Type::builtin(Type::Bases_Float), false};
        }
        case Expr::Kind::StrLit:
            return {"\"" + escapeCString(e.stringValue) + "\"",
                    Type::pointer(Type::builtin(Type::Bases_Char)), false};
        case Expr::Kind::BoolLit:
            return {e.boolValue ? "1" : "0", Type::builtin(Type::Bases_Bool), false};
        case Expr::Kind::NullLit:
            return {"0", Type::pointer(Type::builtin(Type::Bases_Void)), false};
        case Expr::Kind::This:
            return {"self", Type::named(currentClass_ ? currentClass_->name : ""), true};
        case Expr::Kind::Identifier: {
            TypePtr local = lookupVar(e.name);
            if (local)
                return {e.name, local, isPointerLike(local)};
            if (currentClass_) {
                auto f = currentClass_->fieldByName.find(e.name);
                if (f != currentClass_->fieldByName.end())
                    return {"self->" + e.name, f->second->type,
                            isPointerLike(f->second->type)};
                for (auto* sf : currentClass_->staticFields)
                    if (sf->name == e.name)
                        return {currentClass_->cname + "__" + e.name, sf->type,
                                isPointerLike(sf->type)};
            }
            auto g = globals_.find(e.name);
            if (g != globals_.end()) {
                TypePtr t = g->second->type;
                return {e.name, t, isPointerLike(t)};
            }
            auto ec = enumConsts_.find(e.name);
            if (ec != enumConsts_.end())
                return {std::to_string(ec->second), Type::builtin(Type::Bases_Int), false};
            if (classes_.count(e.name))
                return {classCName(e.name), Type::named(e.name), true};
            return {e.name, nullptr, false};
        }
        case Expr::Kind::Unary: return emitUnary(e);
        case Expr::Kind::Binary: return emitBinary(e);
        case Expr::Kind::Assign: {
            // String[index] = ... is read-only
            if (e.lhs && e.lhs->kind == Expr::Kind::Index && e.lhs->object) {
                Val obj = emitExpr(*e.lhs->object);
                if (isStringType(stripRef(obj.type)))
                    err("String elements are read-only (use charAt)", e.loc);
            }
            Val lhs = emitExpr(*e.lhs);
            // String assignment / compound assignment
            if (isStringType(stripRef(lhs.type))) {
                std::string dst = lhs.ptr ? lhs.code : "&" + lhs.code;
                if (e.assignOp == AssignOp::Assign)
                    return {stringAssignCall(dst, *e.rhs), lhs.type, false};
                if (e.assignOp == AssignOp::Add)
                    return {stringAppendCall(dst, *e.rhs), lhs.type, false};
                err("String does not support this compound assignment", e.loc);
            }
            Val rhs = emitExpr(*e.rhs);
            std::string op = "=";
            switch (e.assignOp) {
                case AssignOp::Assign: op = "="; break;
                case AssignOp::Add: op = "+="; break;
                case AssignOp::Sub: op = "-="; break;
                case AssignOp::Mul: op = "*="; break;
                case AssignOp::Div: op = "/="; break;
                case AssignOp::Mod: op = "%="; break;
                case AssignOp::Shl: op = "<<="; break;
                case AssignOp::Shr: op = ">>="; break;
                case AssignOp::BitAnd: op = "&="; break;
                case AssignOp::BitXor: op = "^="; break;
                case AssignOp::BitOr: op = "|="; break;
            }
            // class operator= / compound assignment overload
            if (e.assignOp != AssignOp::Assign) {
                const ClassInfo* ci = classOf(stripRef(lhs.type));
                std::string key = std::string("operator") + binSym(
                    e.assignOp == AssignOp::Add ? BinOp::Add :
                    e.assignOp == AssignOp::Sub ? BinOp::Sub :
                    e.assignOp == AssignOp::Mul ? BinOp::Mul :
                    e.assignOp == AssignOp::Div ? BinOp::Div :
                    e.assignOp == AssignOp::Mod ? BinOp::Mod :
                    e.assignOp == AssignOp::BitAnd ? BinOp::BitAnd :
                    e.assignOp == AssignOp::BitXor ? BinOp::BitXor : BinOp::BitOr);
                std::string compound;
                {
                    static const std::unordered_map<int, const char*> m = {
                        {0, "operator+="}, {1, "operator-="}, {2, "operator*="},
                        {3, "operator/="}, {4, "operator%="}, {5, "operator&="},
                        {6, "operator^="}, {7, "operator|="}};
                    compound = m.at(static_cast<int>(e.assignOp) - static_cast<int>(AssignOp::Add));
                }
                if (ci) {
                    auto mit = ci->methods.find(compound);
                    if (mit != ci->methods.end() && !mit->second.empty()) {
                        std::string recv = lhs.code;
                        std::string call = mangleFunction(*mit->second.front(), ci) +
                                           "(&" + recv + ", " + rhs.code + ")";
                        return {call, mit->second.front()->returns, false};
                    }
                }
                (void)key;
            }
            return {"(" + lhs.code + " " + op + " " + rhs.code + ")", lhs.type, false};
        }
        case Expr::Kind::Conditional: {
            Val c = emitExpr(*e.cond);
            Val a = emitExpr(*e.thenExpr);
            Val b = emitExpr(*e.elseExpr);
            return {"(" + c.code + " ? " + a.code + " : " + b.code + ")",
                    a.type ? a.type : b.type, false};
        }
        case Expr::Kind::Comma: {
            std::string s = "(";
            s += emitExpr(*e.lhs).code;
            for (auto& a : e.args) s += ", " + emitExpr(*a).code;
            s += ")";
            return {s, nullptr, false};
        }
        case Expr::Kind::Member: return emitMember(e, false);
        case Expr::Kind::Index: {
            Val o = emitExpr(*e.object);
            Val i = emitExpr(*e.operand);
            if (isStringType(stripRef(o.type))) {
                std::string r = o.ptr ? o.code : "&" + o.code;
                return {"cppic_string_char_at(" + r + ", " + i.code + ")",
                        Type::builtin(Type::Bases_Char), false};
            }
            const ClassInfo* ci = classOfPointee(o.type);
            if (ci) {
                auto mit = ci->methods.find("operator[]");
                if (mit != ci->methods.end() && !mit->second.empty()) {
                    std::string recv = o.ptr ? o.code : "&" + o.code;
                    return {mangleFunction(*mit->second.front(), ci) + "(" + recv +
                                ", " + i.code + ")",
                            mit->second.front()->returns, false};
                }
            }
            TypePtr et = o.type;
            if (et && (et->kind == Type::Kind::Pointer))
                et = std::get<Type::PointerData>(et->data).pointee;
            else if (et && et->kind == Type::Kind::Array)
                et = std::get<Type::ArrayData>(et->data).element;
            return {o.code + "[" + i.code + "]", et, isPointerLike(et)};
        }
        case Expr::Kind::Call: return emitCall(e);
        case Expr::Kind::Cast: {
            Val v = emitExpr(*e.operand);
            std::string t = e.type ? typeToC(*e.type) : "int";
            if (e.type && e.type->kind == Type::Kind::Builtin &&
                std::get<Type::BuiltinData>(e.type->data).base == Type::Bases_Bool)
                t = "unsigned char";
            return {"((" + t + ")(" + v.code + "))", e.type, isPointerLike(e.type)};
        }
        case Expr::Kind::SizeOf: {
            if (e.type) return {"sizeof(" + typeToC(*e.type) + ")",
                                Type::builtin(Type::Bases_Int), false};
            Val v = emitExpr(*e.operand);
            return {"sizeof(" + v.code + ")", Type::builtin(Type::Bases_Int), false};
        }
        case Expr::Kind::NewExpr:
            // Standalone new inside of a larger expression: allocate and mock.
            // Declarations / assignments / delete are lowered to call the ctor
            // (or dtor) via emitDeclaration / emitStmt.
            return {"cppic_malloc(" + newSizeExpr(e) + ")",
                    Type::pointer(e.type), true};
        case Expr::Kind::DeleteExpr: {
            Val v = emitExpr(*e.operand);
            return {"cppic_free(" + v.code + ")", Type::builtin(Type::Bases_Void), false};
        }
    }
    return {"0", nullptr, false};
}

std::string Transpiler::newSizeExpr(const Expr& ne) {
    std::string base = "sizeof(" + typeToC(*ne.type) + ")";
    if (ne.isArrayNew) {
        std::string cnt = ne.newCount ? emitExpr(*ne.newCount).code : "1";
        base = base + " * " + cnt;
    }
    return "((unsigned int)(" + base + "))";
}

std::string Transpiler::newCtorCall(const Expr& ne, const std::string& ptrCode) {
    if (ne.isArrayNew) return "";
    if (isStringType(ne.type)) {
        if (!ne.args.empty() && ne.args[0]->kind == Expr::Kind::StrLit)
            return "cppic_string_set(" + ptrCode + ", \"" +
                   escapeCString(ne.args[0]->stringValue) + "\");";
        return "";
    }
    const ClassInfo* ci = classOf(ne.type);
    if (!ci || ci->ctorOverloads.empty()) return "";
    const FunctionDecl* ctor = ci->ctorOverloads.front();
    std::string args = emitArgsAsValues(ne.args, &ctor->params, "");
    std::string call = mangleFunction(*ctor, ci) + "(" + ptrCode;
    if (!args.empty()) call += ", " + args;
    return call + ");";
}

void Transpiler::emitNewInit(const VarDecl& v, const Expr& ne, int indent) {
    std::string pad = indentStr(indent);
    out_ << pad << typeToC(*v.type) << " " << v.name
         << " = cppic_malloc(" << newSizeExpr(ne) << ");\n";
    std::string ctor = newCtorCall(ne, v.name);
    if (!ctor.empty()) out_ << pad << ctor << "\n";
    declareVar(v.name, v.type);
}

void Transpiler::emitNewAssignment(const Expr& as, int indent) {
    std::string pad = indentStr(indent);
    Val lhs = emitExpr(*as.lhs);
    const Expr& ne = *as.rhs;
    out_ << pad << "((" << lhs.code << ") = cppic_malloc(" << newSizeExpr(ne)
         << "));\n";
    std::string ctor = newCtorCall(ne, lhs.code);
    if (!ctor.empty()) out_ << pad << ctor << "\n";
}

void Transpiler::emitDeleteStatement(const Expr& de, int indent) {
    std::string pad = indentStr(indent);
    Val v = emitExpr(*de.operand);
    TypePtr et = v.type;
    if (et && et->kind == Type::Kind::Pointer)
        et = std::get<Type::PointerData>(et->data).pointee;
    const ClassInfo* dci = classOf(et);
    if (dci && dci->dtor && dci->dtor->body && !de.isArrayDelete)
        out_ << pad << dci->cname << "__dtor(" << v.code << ");\n";
    out_ << pad << "cppic_free(" << v.code << ");\n";
}

Transpiler::Val Transpiler::emitUnary(const Expr& e) {
    Val v = emitExpr(*e.operand);
    switch (e.unOp) {
        case UnOp::Neg: return {"(-" + v.code + ")", v.type, false};
        case UnOp::Plus: return {"(+" + v.code + ")", v.type, false};
        case UnOp::Not: return {"(!" + v.code + ")", Type::builtin(Type::Bases_Bool), false};
        case UnOp::BitNot: return {"(~" + v.code + ")", v.type, false};
        case UnOp::Deref: {
            TypePtr t = derefType(v.type);
            return {"(*" + v.code + ")", t, isPointerLike(t)};
        }
        case UnOp::AddrOf:
            return {"&" + v.code, Type::pointer(v.type), false};
        case UnOp::PreInc:
            if (isClassType(stripRef(v.type))) {
                const ClassInfo* ci = classOf(stripRef(v.type));
                auto m = ci->methods.find("operator++");
                if (m != ci->methods.end() && !m->second.empty())
                    return {mangleFunction(*m->second.front(), ci) + "(&" + v.code + ")",
                            m->second.front()->returns, false};
            }
            return {"(++" + v.code + ")", v.type, false};
        case UnOp::PreDec:
            return {"(--" + v.code + ")", v.type, false};
        case UnOp::PostInc: return {"(" + v.code + "++)", v.type, false};
        case UnOp::PostDec: return {"(" + v.code + "--)", v.type, false};
    }
    return {"0", nullptr, false};
}

Transpiler::Val Transpiler::emitBinary(const Expr& e) {
    Val a = emitExpr(*e.lhs);
    Val b = emitExpr(*e.rhs);
    std::string sym = binSym(e.binOp);

    // String concatenation / comparisons
    {
        bool aStr = isStringType(stripRef(a.type));
        bool bStr = isStringType(stripRef(b.type));
        if (e.binOp == BinOp::Add && (aStr || bStr))
            err("String '+' is only supported in assignments, initializers and "
                "return statements",
                e.loc);
        if (e.binOp == BinOp::Eq || e.binOp == BinOp::Ne ||
            e.binOp == BinOp::Lt || e.binOp == BinOp::Le ||
            e.binOp == BinOp::Gt || e.binOp == BinOp::Ge) {
            if (aStr || bStr) {
                std::string ar = a.ptr ? a.code : "&" + a.code;
                std::string br = b.ptr ? b.code : "&" + b.code;
                std::string cmp;
                std::string rel;
                if (aStr) {
                    cmp = bStr ? "cppic_string_compare(" + ar + ", " + br + ")"
                               : "cppic_string_compare_lit(" + ar + ", " + b.code + ")";
                    rel = sym;
                } else {
                    // "lit" < s  <=>  compare(b, lit) > 0 (keep literal on the left)
                    cmp = "cppic_string_compare_lit(" + br + ", " + a.code + ")";
                    switch (e.binOp) {
                        case BinOp::Eq: rel = "=="; break;
                        case BinOp::Ne: rel = "!="; break;
                        case BinOp::Lt: rel = ">"; break;
                        case BinOp::Le: rel = ">="; break;
                        case BinOp::Gt: rel = "<"; break;
                        case BinOp::Ge: rel = "<="; break;
                        default: rel = "=="; break;
                    }
                }
                return {"(" + cmp + " " + rel + " 0)",
                        Type::builtin(Type::Bases_Bool), false};
            }
        }
    }

    // class operator overload
    const ClassInfo* ci = classOf(stripRef(a.type));
    if (ci) {
        auto m = ci->methods.find("operator" + sym);
        if (m != ci->methods.end() && !m->second.empty()) {
            std::string recv = a.ptr ? a.code : "&" + a.code;
            return {mangleFunction(*m->second.front(), ci) + "(" + recv + ", " +
                        b.code + ")",
                    m->second.front()->returns, false};
        }
    }
    const ClassInfo* ci2 = classOf(stripRef(b.type));
    if (ci2) {
        auto m = ci2->methods.find("operator" + sym);
        if (m != ci2->methods.end() && !m->second.empty()) {
            std::string recv = b.ptr ? b.code : "&" + b.code;
            return {mangleFunction(*m->second.front(), ci2) + "(" + recv + ", " +
                        a.code + ")",
                    m->second.front()->returns, false};
        }
    }

    TypePtr rt = Type::builtin(Type::Bases_Int);
    if (e.binOp == BinOp::LAnd || e.binOp == BinOp::LOr ||
        e.binOp == BinOp::Lt || e.binOp == BinOp::Gt || e.binOp == BinOp::Le ||
        e.binOp == BinOp::Ge || e.binOp == BinOp::Eq || e.binOp == BinOp::Ne ||
        e.binOp == BinOp::BitAnd || e.binOp == BinOp::BitOr || e.binOp == BinOp::BitXor)
        rt = a.type ? a.type : b.type;
    else
        rt = a.type ? a.type : b.type;
    return {"(" + a.code + " " + sym + " " + b.code + ")", rt, false};
}

Transpiler::Val Transpiler::emitMember(const Expr& e, bool wantCall) {
    (void)wantCall;
    Val o = emitExpr(*e.object);
    const ClassInfo* ci = classOfPointee(o.type);

    // static member via Class::member
    if (!ci && e.object && e.object->kind == Expr::Kind::Identifier) {
        auto cls = classes_.find(e.object->name);
        if (cls != classes_.end()) {
            for (auto* sf : cls->second.staticFields)
                if (sf->name == e.member)
                    return {cls->second.cname + "__" + e.member, sf->type,
                            isPointerLike(sf->type)};
            return {cls->second.cname + "__" + e.member, nullptr, false};
        }
    }

    if (ci) {
        auto f = ci->fieldByName.find(e.member);
        if (f != ci->fieldByName.end()) {
            std::string access = o.ptr ? "->" : ".";
            return {o.code + access + e.member, f->second->type,
                    isPointerLike(f->second->type)};
        }
        for (auto* sf : ci->staticFields)
            if (sf->name == e.member)
                return {ci->cname + "__" + e.member, sf->type, isPointerLike(sf->type)};
        if (ci->methods.count(e.member))
            return {ci->cname + "__" + e.member, nullptr, false};
        err("class '" + ci->name + "' has no member '" + e.member + "'", e.loc);
    }

    // unknown struct or pointer
    std::string access = o.ptr ? "->" : ".";
    return {o.code + access + e.member, nullptr, false};
}

Transpiler::Val Transpiler::emitCall(const Expr& e) {
    const Expr* callee = e.object.get();

    // method call: obj.method(...) / ptr->method(...) / Class::static(...)
    if (callee->kind == Expr::Kind::Member) {
        const std::string& member = callee->member;
        Val recv = emitExpr(*callee->object);

        // String method call: length / charAt / c_str / isEmpty / indexOf /
        // startsWith / endsWith
        if (isStringType(stripRef(recv.type))) {
            std::string r = recv.ptr ? recv.code : "&" + recv.code;
            const std::string& fn = stringFunctionName(member);
            if (fn.empty())
                err("String has no method '" + member + "'", e.loc);
            if (member == "indexOf" && !e.args.empty() &&
                e.args[0]->kind == Expr::Kind::StrLit)
                return {"cppic_string_index_of(" + r + ", " +
                            emitExpr(*e.args[0]).code + ")",
                        Type::builtin(Type::Bases_Int), false};
            std::string args = emitArgsAsValues(e.args, nullptr, "");
            TypePtr ret = Type::builtin(Type::Bases_Int);
            bool ptr = false;
            const std::string& member2 = member;
            if (member2 == "charAt") ret = Type::builtin(Type::Bases_Char);
            else if (member2 == "isEmpty" || member2 == "startsWith" ||
                     member2 == "endsWith")
                ret = Type::builtin(Type::Bases_Bool);
            else if (member2 == "c_str") {
                ret = Type::pointer(Type::builtin(Type::Bases_Char));
                ptr = true;
            }
            std::string call = fn + "(" + r;
            if (!args.empty()) call += ", " + args;
            call += ")";
            return {call, ret, ptr};
        }

        const ClassInfo* ci = classOfPointee(recv.type);

        // static: Class::method(...)
        if (!ci && callee->object->kind == Expr::Kind::Identifier) {
            auto cls = classes_.find(callee->object->name);
            if (cls != classes_.end()) ci = &cls->second;
        }

        if (ci) {
            auto mit = ci->methods.find(member);
            if (mit == ci->methods.end() && member.rfind("operator", 0) == 0) {
                // handled by emitBinary/index normally
            }
            if (mit != ci->methods.end() && !mit->second.empty()) {
                const FunctionDecl* fn = mit->second.front();
                std::string args = emitArgsAsValues(e.args, &fn->params, "");
                std::string call;
                if (fn->isStatic)
                    call = mangleFunction(*fn, ci) + "(" + args + ")";
                else {
                    std::string recvPtr = recv.ptr ? recv.code : "&" + recv.code;
                    if (args.empty())
                        call = mangleFunction(*fn, ci) + "(" + recvPtr + ")";
                    else
                        call = mangleFunction(*fn, ci) + "(" + recvPtr + ", " + args + ")";
                }
                return {call, fn->returns, isPointerLike(fn->returns)};
            }
            // field holding a callable
            auto fld = ci->fieldByName.find(member);
            if (fld != ci->fieldByName.end()) {
                std::string access = recv.ptr ? "->" : ".";
                std::string args = emitArgsAsValues(e.args, nullptr, "");
                return {recv.code + access + member + "(" + args + ")", nullptr, false};
            }
            err("class '" + ci->name + "' has no method '" + member + "'", e.loc);
        }

        // unknown object: emit obj.method(...)
        std::string access = recv.ptr ? "->" : ".";
        std::string args = emitArgsAsValues(e.args, nullptr, "");
        return {recv.code + access + member + "(" + args + ")", nullptr, false};
    }

    // free function or constructor-like call
    if (callee->kind == Expr::Kind::Identifier) {
        auto it = freeFuncs_.find(callee->name);
        if (it != freeFuncs_.end() && !it->second.empty()) {
            const FunctionDecl* fn = it->second.front();
            std::string args = emitArgsAsValues(e.args, &fn->params, "");
            return {mangleName(fn->name) + "(" + args + ")", fn->returns,
                    isPointerLike(fn->returns)};
        }
        std::string args = emitArgsAsValues(e.args, nullptr, "");
        return {callee->name + "(" + args + ")", nullptr, false};
    }

    std::string calleeCode = emitExpr(*callee).code;
    std::string args = emitArgsAsValues(e.args, nullptr, "");
    return {calleeCode + "(" + args + ")", nullptr, false};
}

std::string Transpiler::emitArgsAsValues(const std::vector<ExprPtr>& args,
                                         const std::vector<ParamDecl>* params,
                                         const std::string&) {
    std::string s;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) s += ", ";
        Val v = emitExpr(*args[i]);
        bool wantPtr = false;
        if (params && i < params->size()) {
            const TypePtr& pt = (*params)[i].type;
            if (pt && pt->kind == Type::Kind::Reference && !v.ptr) wantPtr = true;
        }
        s += wantPtr ? "&" + v.code : v.code;
    }
    return s;
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

void Transpiler::emitStmt(const Stmt& s, int indent) {
    std::string pad = indentStr(indent);
    switch (s.kind) {
        case Stmt::Kind::Block: emitBlock(s, indent); break;
        case Stmt::Kind::Empty: out_ << pad << ";\n"; break;
        case Stmt::Kind::Expr:
            if (s.expr->kind == Expr::Kind::DeleteExpr)
                emitDeleteStatement(*s.expr, indent);
            else if (s.expr->kind == Expr::Kind::Assign &&
                     s.expr->rhs && s.expr->rhs->kind == Expr::Kind::NewExpr)
                emitNewAssignment(*s.expr, indent);
            else
                out_ << pad << emitExpr(*s.expr).code << ";\n";
            break;
        case Stmt::Kind::Declaration:
            if (s.decl) emitDeclaration(*s.decl, indent);
            break;
        case Stmt::Kind::If:
            out_ << pad << "if (" << emitExpr(*s.cond).code << ") ";
            emitStmt(*s.body, indent);
            if (s.elseBody) {
                out_ << pad << "else ";
                emitStmt(*s.elseBody, indent);
            }
            break;
        case Stmt::Kind::While:
            out_ << pad << "while (" << emitExpr(*s.cond).code << ") ";
            emitStmt(*s.body, indent);
            break;
        case Stmt::Kind::DoWhile:
            out_ << pad << "do ";
            emitStmt(*s.body, indent);
            out_ << pad << "while (" << emitExpr(*s.cond).code << ");\n";
            break;
        case Stmt::Kind::For: {
            out_ << pad << "for (";
            if (s.initStmt) {
                if (s.initStmt->kind == Stmt::Kind::Declaration && s.initStmt->decl) {
                    const VarDecl& v = *s.initStmt->decl;
                    out_ << typeToC(*v.type) << " " << v.name;
                    if (v.init) out_ << " = " << emitExpr(*v.init).code;
                } else if (s.initStmt->kind == Stmt::Kind::Expr) {
                    out_ << emitExpr(*s.initStmt->expr).code;
                } else if (s.initStmt->kind == Stmt::Kind::Block &&
                           s.initStmt->items.size() == 1 &&
                           s.initStmt->items[0]->decl) {
                    const VarDecl& v = *s.initStmt->items[0]->decl;
                    out_ << typeToC(*v.type) << " " << v.name;
                    if (v.init) out_ << " = " << emitExpr(*v.init).code;
                }
            }
            out_ << "; ";
            if (s.cond) out_ << emitExpr(*s.cond).code;
            out_ << "; ";
            if (s.thenExpr) out_ << emitExpr(*s.thenExpr).code;
            out_ << ") ";
            if (s.body && s.body->kind == Stmt::Kind::Block)
                emitStmt(*s.body, indent);
            else {
                out_ << "\n";
                emitStmt(*s.body, indent + 1);
            }
            break;
        }
        case Stmt::Kind::Switch: {
            out_ << pad << "switch (" << emitExpr(*s.cond).code << ") ";
            emitStmt(*s.body, indent);
            break;
        }
        case Stmt::Kind::Case:
            out_ << indentStr(indent - 1) << "case " << emitExpr(*s.expr).code << ":\n";
            emitStmt(*s.body, indent);
            break;
        case Stmt::Kind::Default:
            out_ << indentStr(indent - 1) << "default:\n";
            emitStmt(*s.body, indent);
            break;
        case Stmt::Kind::Break: out_ << pad << "break;\n"; break;
        case Stmt::Kind::Continue: out_ << pad << "continue;\n"; break;
        case Stmt::Kind::Goto: out_ << pad << "goto " << s.name << ";\n"; break;
        case Stmt::Kind::Label:
            out_ << s.name << ":\n";
            if (s.body) emitStmt(*s.body, indent);
            break;
        case Stmt::Kind::Return: {
            out_ << pad << "return";
            if (s.expr) {
                // returning a reference: *this / *ptr -> pointer
                if (s.expr->kind == Expr::Kind::Unary &&
                    s.expr->unOp == UnOp::Deref) {
                    Val v = emitExpr(*s.expr->operand);
                    out_ << " " << v.code;
                } else {
                    out_ << " " << emitExpr(*s.expr).code;
                }
            }
            out_ << ";\n";
            break;
        }
    }
}

void Transpiler::emitBlock(const Stmt& s, int indent) {
    std::string pad = indentStr(indent);
    out_ << pad << "{\n";
    pushScope();
    for (const auto& item : s.items) emitStmt(*item, indent + 1);
    popScope();
    out_ << pad << "}\n";
}

void Transpiler::emitDeclaration(const VarDecl& v, int indent) {
    std::string pad = indentStr(indent);

    // Arduino-style String local
    if (isStringType(v.type)) {
        out_ << pad << "CppicString " << v.name << " = {0, 0, 0, 0};\n";
        declareVar(v.name, v.type);
        if (v.init && v.init->kind != Expr::Kind::NewExpr)
            out_ << pad << stringAssignCall("&" + v.name, *v.init) << ";\n";
        return;
    }

    // heap-allocated pointer:  T* p = new T(args) / new T[n]
    if (v.init && v.init->kind == Expr::Kind::NewExpr) {
        emitNewInit(v, *v.init, indent);
        return;
    }

    // class-type local with constructor call
    const ClassInfo* ci = classOf(v.type);
    if (ci) {
        // Led led(1);  -> Led led; Led__ctor(&led, 1);
        if (v.init && v.init->kind == Expr::Kind::Call) {
            const Expr& call = *v.init;
            const Expr* ce = call.object.get();
            bool isCtor = false;
            if (ce->kind == Expr::Kind::Identifier && ce->name == ci->name) isCtor = true;
            if (ce->kind == Expr::Kind::Member && ce->object->kind == Expr::Kind::Identifier &&
                ce->object->name == ci->name)
                isCtor = true;
            if (isCtor) {
                out_ << pad << ci->cname << " " << v.name << ";\n";
                declareVar(v.name, v.type);
                std::string args = emitArgsAsValues(call.args, nullptr, "");
                std::string fn = ci->cname + "__ctor";
                if (ci->hasDefaultCtor || !ci->ctorOverloads.empty())
                    fn = mangleFunction(*ci->ctorOverloads.front(), ci);
                if (args.empty())
                    out_ << pad << fn << "(&" << v.name << ");\n";
                else
                    out_ << pad << fn << "(&" << v.name << ", " << args << ");\n";
                return;
            }
        }
        out_ << pad << ci->cname << " " << v.name << ";\n";
        declareVar(v.name, v.type);
        if (ci->hasDefaultCtor)
            out_ << pad << ci->cname << "__ctor(&" << v.name << ");\n";
        else {
            // field initializers: synthesize assignments
            for (auto* f : ci->instanceFields) {
                if (f->init)
                    out_ << pad << v.name << "." << f->name << " = "
                         << emitExpr(*f->init).code << ";\n";
            }
        }
        return;
    }

    // array of class or primitive
    if (v.type && v.type->kind == Type::Kind::Array) {
        const auto& a = std::get<Type::ArrayData>(v.type->data);
        out_ << pad << typeToC(*a.element) << " " << v.name;
        if (a.size) out_ << "[" << *a.size << "]";
        else out_ << "[]";
        if (v.init && v.init->kind == Expr::Kind::StrLit && a.element &&
            a.element->kind == Type::Kind::Builtin &&
            std::get<Type::BuiltinData>(a.element->data).base == Type::Bases_Char)
            out_ << " = \"" << escapeCString(v.init->stringValue) << "\"";
        out_ << ";\n";
        declareVar(v.name, v.type);
        return;
    }

    out_ << pad << typeToC(*v.type) << " " << v.name;
    if (v.init) out_ << " = " << emitExpr(*v.init).code;
    out_ << ";\n";
    declareVar(v.name, v.type);
}

// ---------------------------------------------------------------------------
// Functions / classes
// ---------------------------------------------------------------------------

void Transpiler::emitPrototype(const FunctionDecl& f, const ClassInfo* ci) {
    out_ << typeToC(*f.returns) << " " << mangleFunction(f, ci) << "(";
    bool needComma = false;
    if (ci && !f.isStatic) {
        out_ << ci->cname << "* self";
        needComma = true;
    }
    for (const auto& p : f.params) {
        if (needComma) out_ << ", ";
        needComma = true;
        out_ << typeToC(*p.type);
        if (!p.name.empty()) out_ << " " << p.name;
    }
    if (!needComma) out_ << "void";
    out_ << ");\n";
}

void Transpiler::emitFunction(const FunctionDecl& f, const ClassInfo* ci) {
    TypePtr ret = f.returns;
    if (isStringType(ret))
        err("String cannot be returned by value; pass a String& out parameter "
            "instead",
            f.loc);
    std::string retType = f.isConstructor || f.isDestructor ? "void" : typeToC(*ret);
    out_ << retType << " " << mangleFunction(f, ci) << "(";
    bool needComma = false;
    if (ci && !f.isStatic) {
        out_ << ci->cname << "* self";
        needComma = true;
    }
    for (const auto& p : f.params) {
        if (needComma) out_ << ", ";
        needComma = true;
        out_ << typeToC(*p.type) << " " << p.name;
    }
    if (!needComma) out_ << "void";
    out_ << ") {\n";

    pushScope();
    currentClass_ = ci;
    if (ci && !f.isStatic) declareVar("self", Type::named(ci->name));
    for (const auto& p : f.params)
        if (!p.name.empty()) declareVar(p.name, p.type);
    currentReturns_ = ret;

    if (f.body) {
        for (const auto& item : f.body->items) emitStmt(*item, 1);
    }
    popScope();
    currentClass_ = nullptr;
    out_ << "}\n\n";
}

void Transpiler::emitClassStruct(const ClassInfo& ci) {
    if (ci.isUnion)
        out_ << "typedef union " << ci.cname << " " << ci.cname << ";\n";
    else
        out_ << "typedef struct " << ci.cname << " " << ci.cname << ";\n";
}

void Transpiler::emitGlobalVar(const VarDecl& v) {
    if (isStringType(v.type)) {
        if (v.init && v.init->kind == Expr::Kind::StrLit) {
            const std::string& lit = v.init->stringValue;
            out_ << "CppicString " << v.name << " = {(char*)\""
                 << escapeCString(lit) << "\", " << static_cast<unsigned short>(lit.size())
                 << ", " << static_cast<unsigned short>(lit.size()) << ", 0};\n";
        } else {
            out_ << "CppicString " << v.name << " = {0, 0, 0, 0};\n";
        }
        return;
    }
    out_ << typeToC(*v.type) << " " << v.name;
    if (v.init) out_ << " = " << emitExpr(*v.init).code;
    out_ << ";\n";
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

std::string Transpiler::run(const TranslationUnit& tu) {
    collect(tu);

    out_ << "/* Generated by cppic - do not edit */\n";
    out_ << "#include <stdint.h>\n";
    out_ << "#include \"cppic_runtime.h\"\n\n";

    // forward typedefs, then full struct definitions
    for (const auto& [name, ci] : classes_) emitClassStruct(ci);
    out_ << "\n";
    for (const auto& [name, ci] : classes_) {
        out_ << (ci.isUnion ? "union " : "struct ") << ci.cname << " {\n";
        for (const ClassMember& m : std::vector<ClassMember>()) (void)m;
        // instance fields
        for (auto* f : ci.instanceFields)
            out_ << "    " << typeToC(*f->type) << " " << f->name << ";\n";
        if (ci.instanceFields.empty()) out_ << "    char _empty;\n";
        out_ << "};\n\n";
    }

    // static fields as globals
    for (const auto& [name, ci] : classes_) {
        for (auto* sf : ci.staticFields) {
            out_ << typeToC(*sf->type) << " " << ci.cname << "__" << sf->name;
            if (sf->init) out_ << " = " << emitExpr(*sf->init).code;
            out_ << ";\n";
        }
    }
    out_ << "\n";

    // global variables
    for (const auto& d : tu.decls)
        if (d->kind == Decl::Kind::Var) emitGlobalVar(*d->var);
    out_ << "\n";

    // prototypes
    for (const auto& [name, ci] : classes_) {
        for (auto& [mn, overloads] : ci.methods)
            for (auto* f : overloads)
                if (f->body) emitPrototype(*f, &ci);
        for (auto* f : ci.ctorOverloads)
            if (f->body) emitPrototype(*f, &ci);
        if (ci.dtor && ci.dtor->body) emitPrototype(*ci.dtor, &ci);
    }
    for (const auto& [name, fns] : freeFuncs_)
        for (auto* f : fns)
            if (f->body) emitPrototype(*f, nullptr);
    for (const auto& d : tu.decls) {
        if (d->kind != Decl::Kind::Function) continue;
        const FunctionDecl& f = *d->func;
        auto pos = f.name.find("::");
        if (pos == std::string::npos) continue;
        std::string cls = f.name.substr(0, pos);
        auto it = classes_.find(cls);
        if (it == classes_.end()) continue;
        emitPrototype(f, &it->second);
    }
    out_ << "\n";

    // method definitions (inline in class)
    for (const auto& [name, ci] : classes_) {
        for (const auto& m : ci.methods) {
            for (auto* f : m.second)
                if (f->body) emitFunction(*f, &ci);
        }
        for (auto* f : ci.ctorOverloads)
            if (f->body) emitFunction(*f, &ci);
        if (ci.dtor && ci.dtor->body) emitFunction(*ci.dtor, &ci);
    }

    // out-of-line method definitions
    for (const auto& d : tu.decls) {
        if (d->kind != Decl::Kind::Function) continue;
        const FunctionDecl& f = *d->func;
        auto pos = f.name.find("::");
        if (pos == std::string::npos) continue;
        std::string cls = f.name.substr(0, pos);
        auto it = classes_.find(cls);
        if (it == classes_.end()) { (void)0; continue; }
        emitFunction(f, &it->second);
    }

    // free functions
    for (const auto& d : tu.decls) {
        if (d->kind != Decl::Kind::Function) continue;
        const FunctionDecl& f = *d->func;
        if (f.name.find("::") != std::string::npos) continue;
        emitFunction(f, nullptr);
    }

    return out_.str();
}

}  // namespace cppic