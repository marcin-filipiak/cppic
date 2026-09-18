#pragma once

#include "lexer/token.hpp"

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cppic {

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct Type : std::enable_shared_from_this<Type> {
    enum class Kind {
        Builtin,   // void/bool/char/int/... (+ const/volatile qualifiers)
        Pointer,   // T*
        Reference, // T&
        Array,     // T[N]
Named,   // refers to a class/struct/union/enum/typedef name
        Function,  // used for codegen types
    };

    Kind kind = Kind::Builtin;

    // Builtin
    enum Base {
        Bases_Void,
        Bases_Bool,
        Bases_Char,
        Bases_Int,
        Bases_Float,
        Bases_Double,
        Bases_Unset,
    };

    struct BuiltinData {
        Base base = Bases_Unset;
        bool isUnsigned = false;
        bool isShort = false;
        bool isLong = false;
        bool isConst = false;
        bool isVolatile = false;
    };
    struct PointerData {
        TypePtr pointee;
        bool isConst = false;
    };
    struct ReferenceData {
        TypePtr referent;
    };
    struct ArrayData {
        TypePtr element;
        // size: empty if unknown (e.g. function parameter decay)
        std::optional<std::size_t> size;
    };
    struct NamedData {
        std::string name;
    };
    struct FunctionData {
        TypePtr returns;
        std::vector<TypePtr> params;
        bool isVarArg = false;
    };

    std::variant<BuiltinData, PointerData, ReferenceData, ArrayData, NamedData,
                 FunctionData>
        data{BuiltinData{}};

    static TypePtr builtin(Base b) {
        auto t = std::make_shared<Type>();
        t->kind = Kind::Builtin;
        t->data = BuiltinData{b, false, false, false, false, false};
        return t;
    }
    static TypePtr pointer(TypePtr p) {
        auto t = std::make_shared<Type>();
        t->kind = Kind::Pointer;
        t->data = PointerData{std::move(p), false};
        return t;
    }
    static TypePtr reference(TypePtr r) {
        auto t = std::make_shared<Type>();
        t->kind = Kind::Reference;
        t->data = ReferenceData{std::move(r)};
        return t;
    }
    static TypePtr array(TypePtr e, std::optional<std::size_t> n) {
        auto t = std::make_shared<Type>();
        t->kind = Kind::Array;
        t->data = ArrayData{std::move(e), n};
        return t;
    }
    static TypePtr named(std::string n) {
        auto t = std::make_shared<Type>();
        t->kind = Kind::Named;
        t->data = NamedData{std::move(n)};
        return t;
    }
    static TypePtr function(TypePtr r, std::vector<TypePtr> p, bool varArg = false) {
        auto t = std::make_shared<Type>();
        t->kind = Kind::Function;
        t->data = FunctionData{std::move(r), std::move(p), varArg};
        return t;
    }
};

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

enum class UnOp { Neg, Plus, Not, BitNot, Deref, AddrOf, PreInc, PreDec, PostInc, PostDec };
enum class BinOp {
    Mul, Div, Mod,          Add, Sub,
    Shl, Shr,
    Lt, Gt, Le, Ge,
    Eq, Ne,
    BitAnd, BitXor, BitOr,
    LAnd, LOr,
};
enum class AssignOp { Assign, Mul, Div, Mod, Add, Sub, Shl, Shr, BitAnd, BitXor, BitOr };
enum class CastKind { C, Static, Reinterpret, Const };

struct Expr {
    enum class Kind {
        IntLit, FloatLit, StrLit, BoolLit, NullLit,
        Identifier, This,
        Unary, Binary, Assign, Conditional, Comma,
        Member, Index, Call,
        Cast, SizeOf, NewExpr, DeleteExpr,
    };
    Kind kind = Kind::IntLit;
    SourceLoc loc;

    // IntLit / CharLit
    bool isUnsignedNum = false;
    bool isLongNum = false;
    unsigned long long intValue = 0;
    // FloatLit
    double floatValue = 0;
    // StrLit
    std::string stringValue;
    // BoolLit
    bool boolValue = false;
    // Identifier
    std::string name;
    // Unary / Binary / Assign / Cast etc.
    UnOp unOp = UnOp::Neg;
    BinOp binOp = BinOp::Add;
    AssignOp assignOp = AssignOp::Assign;
    CastKind castKind = CastKind::C;
    ExprPtr operand;   // unary
    ExprPtr lhs, rhs;  // binary / assign
    ExprPtr cond, thenExpr, elseExpr;  // conditional
    std::vector<ExprPtr> args;         // comma / call args
    ExprPtr object;                    // member/index access target
    TypePtr type;                      // cast/new/sizeof(type)
    bool isArrayNew = false;
    bool isArrayDelete = false;
    bool arrowAccess = false;          // '->' member access
    ExprPtr newCount;                  // array new count
    std::string member;                // member name

    static ExprPtr make(Kind k, SourceLoc loc) {
        auto e = std::make_unique<Expr>();
        e->kind = k;
        e->loc = loc;
        return e;
    }
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct VarDecl;
using VarDeclPtr = std::shared_ptr<VarDecl>;
struct FunctionDecl;
using FunctionDeclPtr = std::shared_ptr<FunctionDecl>;

struct Stmt {
    enum class Kind {
        Block, Empty, Expr, Declaration,
        If, While, DoWhile, For, Switch,
        Case, Default, Break, Continue, Goto, Label,
        Return,
    };
    Kind kind = Kind::Empty;
    SourceLoc loc;

    std::vector<StmtPtr> items;   // block / switch body
    ExprPtr expr;                 // expr / return value / for init/cond/incr
    ExprPtr cond, thenExpr;       // if / while / for / do-while
    StmtPtr body, elseBody;       // if / while / for
    StmtPtr initStmt;             // for init statement / declaration
    std::string name;             // goto target / label
    std::shared_ptr<VarDecl> decl; // declaration statement

    static StmtPtr make(Kind k, SourceLoc loc) {
        auto s = std::make_unique<Stmt>();
        s->kind = k;
        s->loc = loc;
        return s;
    }
};

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

struct Decl;
using DeclPtr = std::shared_ptr<Decl>;

struct ClassDecl;
struct EnumDecl;

struct ParamDecl {
    TypePtr type;
    std::string name;
    ExprPtr defaultValue;
};

struct VarDecl : std::enable_shared_from_this<VarDecl> {
    TypePtr type;
    std::string name;
    ExprPtr init;
    bool isStatic = false;
    bool isExtern = false;
    bool isConstexpr = false;
    SourceLoc loc;
};

struct FunctionDecl : std::enable_shared_from_this<FunctionDecl> {
    TypePtr returns;
    std::string name;
    std::vector<ParamDecl> params;
    StmtPtr body;
    bool isStatic = false;
    bool isInline = false;
    bool isVirtual = false;
    bool isConstMethod = false;
    bool isConstructor = false;
    bool isDestructor = false;
    bool isOperator = false;
    TT operatorToken = TT::End;
    SourceLoc loc;
};

struct ClassMember {
    enum class Kind { Field, Method, NestedClass, NestedEnum };
    Kind kind = Kind::Field;
    VarDeclPtr field;
    FunctionDeclPtr method;
    std::shared_ptr<ClassDecl> nestedClass;
    std::shared_ptr<EnumDecl> nestedEnum;
    bool isPrivate = false;
};

struct ClassDecl : std::enable_shared_from_this<ClassDecl> {
    std::string name;
    bool isStruct = false;
    bool isUnion = false;
    std::vector<std::string> bases;  // base class names
    std::vector<ClassMember> members;
    bool isTemplateDecl = false;     // placeholder for template<class T>
    SourceLoc loc;
};

struct EnumDecl {
    std::string name;
    // name -> optional explicit value
    struct Enumerator {
        std::string name;
        ExprPtr value;
    };
    std::vector<Enumerator> enumerators;
    SourceLoc loc;
};

struct Decl {
    enum class Kind {
        Var, Function, Class, Enum, Typedef, Using,
        Empty,  // stray semicolon
    };
    Kind kind = Kind::Empty;
    SourceLoc loc;

    VarDeclPtr var;
    FunctionDeclPtr func;
    std::shared_ptr<ClassDecl> klass;
    std::shared_ptr<EnumDecl> enumm;
    TypePtr typedefType;
    std::string typedefName;
    std::string usingTarget;

    static DeclPtr make(Kind k, SourceLoc loc) {
        auto d = std::make_shared<Decl>();
        d->kind = k;
        d->loc = loc;
        return d;
    }
};

struct TranslationUnit {
    std::string filename;
    std::vector<DeclPtr> decls;
};

}  // namespace cppic