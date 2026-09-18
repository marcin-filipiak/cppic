#include "ast/dump.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace cppic {
namespace {

std::string binOpStr(BinOp o) {
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

std::string unOpStr(UnOp o) {
    switch (o) {
        case UnOp::Neg: return "-";
        case UnOp::Plus: return "+";
        case UnOp::Not: return "!";
        case UnOp::BitNot: return "~";
        case UnOp::Deref: return "*";
        case UnOp::AddrOf: return "&";
        case UnOp::PreInc: return "++";
        case UnOp::PreDec: return "--";
        case UnOp::PostInc: return "++";
        case UnOp::PostDec: return "--";
    }
    return "?";
}

std::string assignOpStr(AssignOp o) {
    switch (o) {
        case AssignOp::Assign: return "=";
        case AssignOp::Mul: return "*=";
        case AssignOp::Div: return "/=";
        case AssignOp::Mod: return "%=";
        case AssignOp::Add: return "+=";
        case AssignOp::Sub: return "-=";
        case AssignOp::Shl: return "<<=";
        case AssignOp::Shr: return ">>=";
        case AssignOp::BitAnd: return "&=";
        case AssignOp::BitXor: return "^=";
        case AssignOp::BitOr: return "|=";
    }
    return "?";
}

std::string typeName(const Type& t) {
    switch (t.kind) {
        case Type::Kind::Builtin: {
            const auto& b = std::get<Type::BuiltinData>(t.data);
            std::string s;
            if (b.isConst) s += "const ";
            if (b.isVolatile) s += "volatile ";
            if (b.isUnsigned) s += "unsigned ";
            if (b.isShort) s += "short ";
            if (b.isLong) s += "long ";
            switch (b.base) {
                case Type::Bases_Void: s += "void"; break;
                case Type::Bases_Bool: s += "bool"; break;
                case Type::Bases_Char: s += "char"; break;
                case Type::Bases_Int: s += "int"; break;
                case Type::Bases_Float: s += "float"; break;
                case Type::Bases_Double: s += "double"; break;
                case Type::Bases_Unset: s += "?"; break;
            }
            return s;
        }
        case Type::Kind::Pointer:
            return dumpType(*std::get<Type::PointerData>(t.data).pointee) + "*";
        case Type::Kind::Reference:
            return dumpType(*std::get<Type::ReferenceData>(t.data).referent) + "&";
        case Type::Kind::Array: {
            const auto& a = std::get<Type::ArrayData>(t.data);
            std::string s = dumpType(*a.element);
            if (a.size)
                s += "[" + std::to_string(*a.size) + "]";
            else
                s += "[]";
            return s;
        }
        case Type::Kind::Named:
            return std::get<Type::NamedData>(t.data).name;
        case Type::Kind::Function:
            return "func";
    }
    return "?";
}

}  // namespace

std::string dumpType(const Type& t) { return typeName(t); }

std::string dumpExpr(const Expr& e) {
    std::ostringstream os;
    switch (e.kind) {
        case Expr::Kind::IntLit: os << e.intValue; break;
        case Expr::Kind::FloatLit: os << e.floatValue; break;
        case Expr::Kind::StrLit: os << "\"" << e.stringValue << "\""; break;
        case Expr::Kind::BoolLit: os << (e.boolValue ? "true" : "false"); break;
        case Expr::Kind::NullLit: os << "nullptr"; break;
        case Expr::Kind::Identifier: os << e.name; break;
        case Expr::Kind::This: os << "this"; break;
        case Expr::Kind::Unary:
            os << (e.unOp == UnOp::PostInc || e.unOp == UnOp::PostDec
                       ? "(" + dumpExpr(*e.operand) + unOpStr(e.unOp) + ")"
                       : "(" + unOpStr(e.unOp) + dumpExpr(*e.operand) + ")");
            break;
        case Expr::Kind::Binary:
            os << "(" << dumpExpr(*e.lhs) << " " << binOpStr(e.binOp) << " "
               << dumpExpr(*e.rhs) << ")";
            break;
        case Expr::Kind::Assign:
            os << "(" << dumpExpr(*e.lhs) << " " << assignOpStr(e.assignOp)
               << " " << dumpExpr(*e.rhs) << ")";
            break;
        case Expr::Kind::Conditional:
            os << "(" << dumpExpr(*e.cond) << " ? " << dumpExpr(*e.thenExpr)
               << " : " << dumpExpr(*e.elseExpr) << ")";
            break;
        case Expr::Kind::Comma: {
            os << "(" << dumpExpr(*e.lhs);
            for (auto& a : e.args) os << ", " << dumpExpr(*a);
            os << ")";
            break;
        }
        case Expr::Kind::Member:
            os << dumpExpr(*e.object) << (e.arrowAccess ? "->" : ".") << e.member;
            break;
        case Expr::Kind::Index:
            os << dumpExpr(*e.object) << "[" << dumpExpr(*e.operand) << "]";
            break;
        case Expr::Kind::Call: {
            os << dumpExpr(*e.object) << "(";
            bool first = true;
            for (auto& a : e.args) {
                if (!first) os << ", ";
                first = false;
                os << dumpExpr(*a);
            }
            os << ")";
            break;
        }
        case Expr::Kind::Cast: {
            os << "cast<";
            if (e.castKind == CastKind::Static) os << "static";
            else if (e.castKind == CastKind::Reinterpret) os << "reinterpret";
            else if (e.castKind == CastKind::Const) os << "const";
            else os << "c";
            os << " " << dumpType(*e.type) << ">(" << dumpExpr(*e.operand) << ")";
            break;
        }
        case Expr::Kind::SizeOf: {
            os << "sizeof(";
            if (e.type) os << dumpType(*e.type);
            else os << dumpExpr(*e.operand);
            os << ")";
            break;
        }
        case Expr::Kind::NewExpr:
            os << "new " << (e.isArrayNew ? "[] " : "") << dumpType(*e.type);
            if (e.newCount) os << "[" << dumpExpr(*e.newCount) << "]";
            if (!e.args.empty()) {
                os << "(";
                bool first = true;
                for (auto& a : e.args) {
                    if (!first) os << ", ";
                    first = false;
                    os << dumpExpr(*a);
                }
                os << ")";
            }
            break;
        case Expr::Kind::DeleteExpr:
            os << "delete" << (e.isArrayDelete ? "[] " : " ") << dumpExpr(*e.operand);
            break;
    }
    return os.str();
}

std::string dumpStmt(const Stmt& s, int indent) {
    std::ostringstream os;
    std::string pad(indent * 2, ' ');
    std::string pad2((indent + 1) * 2, ' ');

    switch (s.kind) {
        case Stmt::Kind::Block: {
            os << pad << "{\n";
            for (auto& i : s.items) os << dumpStmt(*i, indent + 1);
            os << pad << "}\n";
            break;
        }
        case Stmt::Kind::Empty:
            os << pad << ";\n";
            break;
        case Stmt::Kind::Expr:
            os << pad << dumpExpr(*s.expr) << ";\n";
            break;
        case Stmt::Kind::Declaration:
            os << pad << dumpType(*s.decl->type) << " " << s.decl->name;
            if (s.decl->init) os << " = " << dumpExpr(*s.decl->init);
            os << ";\n";
            break;
        case Stmt::Kind::If:
            os << pad << "if (" << dumpExpr(*s.cond) << ") {\n"
               << dumpStmt(*s.body, indent + 1) << pad << "}";
            if (s.elseBody) os << " else {\n" << dumpStmt(*s.elseBody, indent + 1) << pad << "}";
            os << "\n";
            break;
        case Stmt::Kind::While:
            os << pad << "while (" << dumpExpr(*s.cond) << ") {\n"
               << dumpStmt(*s.body, indent + 1) << pad << "}\n";
            break;
        case Stmt::Kind::DoWhile:
            os << pad << "do {\n" << dumpStmt(*s.body, indent + 1) << pad
               << "} while (" << dumpExpr(*s.cond) << ");\n";
            break;
        case Stmt::Kind::For:
            os << pad << "for (";
            if (s.initStmt) os << dumpStmt(*s.initStmt, indent).substr(2);
            os << " ";
            if (s.cond) os << dumpExpr(*s.cond);
            os << "; ";
            if (s.thenExpr) os << dumpExpr(*s.thenExpr);
            os << ") {\n" << dumpStmt(*s.body, indent + 1) << pad << "}\n";
            break;
        case Stmt::Kind::Switch:
            os << pad << "switch (" << dumpExpr(*s.cond) << ") {\n"
               << dumpStmt(*s.body, indent + 1) << pad << "}\n";
            break;
        case Stmt::Kind::Case:
            os << pad << "case " << dumpExpr(*s.expr) << ":\n"
               << dumpStmt(*s.body, indent + 1);
            break;
        case Stmt::Kind::Default:
            os << pad << "default:\n" << dumpStmt(*s.body, indent + 1);
            break;
        case Stmt::Kind::Break:
            os << pad << "break;\n";
            break;
        case Stmt::Kind::Continue:
            os << pad << "continue;\n";
            break;
        case Stmt::Kind::Goto:
            os << pad << "goto " << s.name << ";\n";
            break;
        case Stmt::Kind::Label:
            os << pad << s.name << ":\n" << dumpStmt(*s.body, indent + 1);
            break;
        case Stmt::Kind::Return:
            os << pad << "return";
            if (s.expr) os << " " << dumpExpr(*s.expr);
            os << ";\n";
            break;
    }
    return os.str();
}

std::string dumpDecl(const Decl& d) {
    std::ostringstream os;
    switch (d.kind) {
        case Decl::Kind::Var:
            os << "var " << dumpType(*d.var->type) << " " << d.var->name;
            if (d.var->init) os << " = " << dumpExpr(*d.var->init);
            os << "\n";
            break;
        case Decl::Kind::Function: {
            const auto& f = *d.func;
            os << (f.isStatic ? "static " : "") << "fn ";
            if (f.isConstructor) os << "<ctor> ";
            if (f.isDestructor) os << "<dtor> ";
            os << dumpType(*f.returns) << " " << f.name << "(";
            bool first = true;
            for (auto& p : f.params) {
                if (!first) os << ", ";
                first = false;
                os << dumpType(*p.type) << " " << p.name;
            }
            os << ")";
            if (f.body) os << " {\n" << dumpStmt(*f.body, 1) << "}";
            os << "\n";
            break;
        }
        case Decl::Kind::Class: {
            const auto& c = *d.klass;
            os << (c.isStruct ? "struct " : "class ") << c.name;
            if (c.isTemplateDecl) os << " <template>";
            os << " {\n";
            for (auto& m : c.members) {
                if (m.kind == ClassMember::Kind::Field) {
                    os << "  " << (m.isPrivate ? "private " : "public ")
                       << "field " << dumpType(*m.field->type) << " " << m.field->name;
                    if (m.field->init) os << " = " << dumpExpr(*m.field->init);
                    os << "\n";
                } else if (m.kind == ClassMember::Kind::Method) {
                    const auto& f = *m.method;
                    os << "  " << (m.isPrivate ? "private " : "public ")
                       << (f.isStatic ? "static " : "")
                       << (f.isVirtual ? "virtual " : "") << "method ";
                    if (f.isConstructor) os << "<ctor> ";
                    if (f.isDestructor) os << "<dtor> ";
                    if (f.isOperator) os << "operator" << tokenName(f.operatorToken) << " ";
                    os << dumpType(*f.returns) << " " << f.name << "(";
                    bool first = true;
                    for (auto& p : f.params) {
                        if (!first) os << ", ";
                        first = false;
                        os << dumpType(*p.type) << " " << p.name;
                    }
                    os << ")";
                    if (f.isConstMethod) os << " const";
                    if (f.body) os << " {\n" << dumpStmt(*f.body, 2) << "  }";
                    os << "\n";
                } else if (m.kind == ClassMember::Kind::NestedClass) {
                    os << "  nested class " << m.nestedClass->name << "\n";
                } else if (m.kind == ClassMember::Kind::NestedEnum) {
                    os << "  nested enum " << m.nestedEnum->name << "\n";
                }
            }
            os << "}\n";
            break;
        }
        case Decl::Kind::Enum: {
            os << "enum " << d.enumm->name << " { ";
            bool first = true;
            for (auto& e : d.enumm->enumerators) {
                if (!first) os << ", ";
                first = false;
                os << e.name << " = " << dumpExpr(*e.value);
            }
            os << " }\n";
            break;
        }
        case Decl::Kind::Typedef:
            os << "typedef " << dumpType(*d.typedefType) << " " << d.typedefName << "\n";
            break;
        case Decl::Kind::Using:
            os << "using " << d.usingTarget << "\n";
            break;
        case Decl::Kind::Empty:
            os << ";\n";
            break;
    }
    return os.str();
}

}  // namespace cppic