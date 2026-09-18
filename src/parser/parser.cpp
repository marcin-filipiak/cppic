#include "parser/parser.hpp"

#include <cctype>
#include <cstdlib>
#include <optional>

namespace cppic {
namespace {

bool isBuiltinTypeToken(TT k) {
    switch (k) {
        case TT::KwVoid:
        case TT::KwBool:
        case TT::KwChar:
        case TT::KwInt:
        case TT::KwFloat:
        case TT::KwDouble:
        case TT::KwShort:
        case TT::KwLong:
        case TT::KwSigned:
        case TT::KwUnsigned:
            return true;
        default:
            return false;
    }
}

bool isDeclSpecKeyword(TT k) {
    switch (k) {
        case TT::KwConst:
        case TT::KwVolatile:
        case TT::KwStatic:
        case TT::KwExtern:
        case TT::KwInline:
        case TT::KwVirtual:
        case TT::KwConstexpr:
        case TT::KwTypedef:
        case TT::KwMutable:
        case TT::KwRegister:
            return true;
        default:
            return false;
    }
}

std::string operatorName(TT k) {
    switch (k) {
        case TT::Plus: return "+";
        case TT::Minus: return "-";
        case TT::Star: return "*";
        case TT::Slash: return "/";
        case TT::Percent: return "%";
        case TT::Amp: return "&";
        case TT::Pipe: return "|";
        case TT::Caret: return "^";
        case TT::Tilde: return "~";
        case TT::Bang: return "!";
        case TT::Assign: return "=";
        case TT::PlusPlus: return "++";
        case TT::MinusMinus: return "--";
        case TT::ShiftLeft: return "<<";
        case TT::ShiftRight: return ">>";
        case TT::EqEq: return "==";
        case TT::NotEq: return "!=";
        case TT::Less: return "<";
        case TT::Greater: return ">";
        case TT::LessEq: return "<=";
        case TT::GreaterEq: return ">=";
        case TT::AmpAmp: return "&&";
        case TT::PipePipe: return "||";
        case TT::PlusEq: return "+=";
        case TT::MinusEq: return "-=";
        case TT::StarEq: return "*=";
        case TT::SlashEq: return "/=";
        case TT::PercentEq: return "%=";
        case TT::AmpEq: return "&=";
        case TT::PipeEq: return "|=";
        case TT::CaretEq: return "^=";
        case TT::ShiftLeftEq: return "<<=";
        case TT::ShiftRightEq: return ">>=";
        case TT::Lparen: return "()";
        case TT::Lbrack: return "[]";
        default: return "";
    }
}

}  // namespace

Parser::Parser(std::vector<Token> tokens) : toks_(std::move(tokens)) {}

const Token& Parser::peek(std::size_t ahead) const {
    std::size_t i = pos_ + ahead;
    if (i >= toks_.size()) return toks_.back();
    return toks_[i];
}

bool Parser::at(TT kind) const { return peek().kind == kind; }

bool Parser::atIdent(const char* text) const {
    const Token& t = peek();
    return t.kind == TT::Identifier && t.text == text;
}

const Token& Parser::next() {
    const Token& t = peek();
    if (t.kind != TT::End) ++pos_;
    return t;
}

bool Parser::match(TT kind) {
    if (at(kind)) {
        ++pos_;
        return true;
    }
    return false;
}

void Parser::expect(TT kind) {
    if (!at(kind)) error(kind);
    ++pos_;
}

Token Parser::expectIdent() {
    const Token& t = peek();
    if (t.kind != TT::Identifier) error(TT::Identifier);
    ++pos_;
    return t;
}

[[noreturn]] void Parser::error(TT kind) {
    std::string got(tokenName(peek().kind));
    std::string want(tokenName(kind));
    throw ParseError{"expected " + want + ", found " + got, peek().loc};
}

[[noreturn]] void Parser::error(const std::string& msg) {
    throw ParseError{msg, peek().loc};
}

// --------------------------------------------------------------------------
// Translation unit
// --------------------------------------------------------------------------

TranslationUnit Parser::parseTranslationUnit() {
    TranslationUnit tu;
    while (!at(TT::End)) {
        if (match(TT::Semicolon)) continue;  // stray ;
        auto decls = parseTopLevel();
        for (auto& d : decls)
            if (d->kind != Decl::Kind::Empty) tu.decls.push_back(std::move(d));
    }
    return tu;
}

// --------------------------------------------------------------------------
// Declarations
// --------------------------------------------------------------------------

std::vector<DeclPtr> Parser::parseTopLevel() {
    const Token& t = peek();

    if (t.kind == TT::KwTemplate) {
        SourceLoc loc = peek().loc;
        next();  // template
        expect(TT::Less);
        int depth = 1;
        while (depth > 0 && !at(TT::End)) {
            if (at(TT::Less)) ++depth;
            if (at(TT::Greater)) --depth;
            ++pos_;
        }
        std::vector<DeclPtr> decls = parseTopLevel();
        for (auto& d : decls) {
            if (d->kind == Decl::Kind::Class) d->klass->isTemplateDecl = true;
            if (d->kind == Decl::Kind::Function) d->func->loc = loc;
            d->loc = loc;
        }
        return decls;
    }

    if (t.kind == TT::KwClass || t.kind == TT::KwStruct || t.kind == TT::KwUnion) {
        DeclPtr d = parseStructOrClass(t.kind);
        std::vector<DeclPtr> out;
        out.push_back(std::move(d));
        return out;
    }

    if (t.kind == TT::KwEnum) {
        DeclPtr d = parseEnum();
        std::vector<DeclPtr> out;
        out.push_back(std::move(d));
        return out;
    }

    if (t.kind == TT::KwTypedef) {
        DeclPtr d = parseTypedef();
        std::vector<DeclPtr> out;
        out.push_back(std::move(d));
        return out;
    }

    if (t.kind == TT::KwUsing) {
        return parseUsing();
    }

    return parseSimpleDeclaration(true);
}

DeclPtr Parser::parseStructOrClass(TT keywordTok) {
    SourceLoc loc = peek().loc;
    bool isStruct = keywordTok == TT::KwStruct;
    bool isUnion = keywordTok == TT::KwUnion;
    next();  // class / struct / union

    std::string className;
    if (peek().kind == TT::Identifier) {
        className = next().text;
        while (match(TT::ColonColon)) className += "::" + expectIdent().text;
    }

    if (match(TT::Colon)) {
        int depth = 0;
        while (!at(TT::End)) {
            TT k = peek().kind;
            if (k == TT::Less) ++depth;
            if (k == TT::Greater) --depth;
            if (k == TT::Lbrace && depth <= 0) break;
            if (k == TT::Semicolon && depth <= 0) break;
            ++pos_;
        }
    }

    if (!at(TT::Lbrace)) {
        expect(TT::Semicolon);  // forward declaration
        auto d = Decl::make(Decl::Kind::Class, loc);
        d->klass = std::make_shared<ClassDecl>();
        d->klass->name = className;
        d->klass->isStruct = isStruct;
        d->klass->isUnion = isUnion;
        return d;
    }

    expect(TT::Lbrace);
    std::vector<ClassMember> members = parseClassBody(className, isStruct, isUnion);
    expect(TT::Rbrace);
    match(TT::Semicolon);

    auto d = Decl::make(Decl::Kind::Class, loc);
    d->klass = std::make_shared<ClassDecl>();
    d->klass->name = className;
    d->klass->isStruct = isStruct;
    d->klass->isUnion = isUnion;
    d->klass->members = std::move(members);
    return d;
}

DeclPtr Parser::parseEnum() {
    SourceLoc loc = peek().loc;
    next();  // enum
    match(TT::KwClass);
    match(TT::KwStruct);

    auto d = Decl::make(Decl::Kind::Enum, loc);
    d->enumm = std::make_shared<EnumDecl>();
    d->enumm->loc = loc;

    if (peek().kind == TT::Identifier || peek().kind == TT::ColonColon) {
        if (peek().kind == TT::Identifier) d->enumm->name = next().text;
        while (match(TT::ColonColon)) d->enumm->name += "::" + expectIdent().text;
    }

    if (!match(TT::Lbrace)) {
        match(TT::Semicolon);
        return d;
    }

    long long nextValue = 0;
    for (;;) {
        EnumDecl::Enumerator e;
        Token n;
        try {
            n = expectIdent();
        } catch (const ParseError&) {
            break;
        }
        e.name = n.text;
        if (match(TT::Assign)) {
            e.value = parseConditional();
            if (e.value && e.value->kind == Expr::Kind::IntLit)
                nextValue = static_cast<long long>(e.value->intValue) + 1;
        } else {
            auto lit = Expr::make(Expr::Kind::IntLit, n.loc);
            lit->intValue = static_cast<unsigned long long>(nextValue);
            e.value = std::move(lit);
            ++nextValue;
        }
        d->enumm->enumerators.push_back(std::move(e));
        if (!match(TT::Comma)) break;
        if (at(TT::Rbrace)) break;  // trailing comma
    }
    expect(TT::Rbrace);
    match(TT::Semicolon);
    return d;
}

DeclPtr Parser::parseTypedef() {
    SourceLoc loc = peek().loc;
    next();  // typedef
    TypePtr type = parseTypeSpec();
    Declarator dec = parseDeclarator(type, /*requireName=*/true);
    match(TT::Semicolon);
    auto d = Decl::make(Decl::Kind::Typedef, loc);
    d->typedefName = dec.name;
    d->typedefType = dec.type;
    return d;
}

std::vector<DeclPtr> Parser::parseUsing() {
    SourceLoc loc = peek().loc;
    next();  // using

    if (match(TT::KwNamespace)) {
        while (at(TT::Identifier) || at(TT::ColonColon)) ++pos_;
        match(TT::Semicolon);
        std::vector<DeclPtr> out;
        out.push_back(Decl::make(Decl::Kind::Empty, loc));
        return out;
    }

    std::string alias = expectIdent().text;
    if (match(TT::Assign)) {
        TypePtr type = parseTypeSpec();
        Declarator dec = parseDeclarator(type, false);
        match(TT::Semicolon);
        auto d = Decl::make(Decl::Kind::Typedef, loc);
        d->typedefName = alias;
        d->typedefType = dec.type;
        std::vector<DeclPtr> out;
        out.push_back(std::move(d));
        return out;
    }
    auto d = Decl::make(Decl::Kind::Empty, loc);
    d->usingTarget = alias;
    std::vector<DeclPtr> out;
    out.push_back(std::move(d));
    return out;
}

TypePtr Parser::parseTypeSpec(bool* sawTypeName) {
    SourceLoc loc = peek().loc;
    (void)loc;

    bool isConst = false;
    bool isVolatile = false;
    while (peek().kind == TT::KwConst || peek().kind == TT::KwVolatile) {
        if (peek().kind == TT::KwConst) isConst = true;
        else isVolatile = true;
        ++pos_;
    }

    if (isBuiltinTypeToken(peek().kind)) {
        TypePtr t = nullptr;
        bool isUnsigned = false, isShort = false, isLong = false;

        while (isBuiltinTypeToken(peek().kind)) {
            TT k = peek().kind;
            ++pos_;
            switch (k) {
                case TT::KwUnsigned: isUnsigned = true; break;
                case TT::KwSigned: isUnsigned = false; break;
                case TT::KwShort: isShort = true; break;
                case TT::KwLong: isLong = true; break;
                case TT::KwVoid: t = Type::builtin(Type::Bases_Void); break;
                case TT::KwBool: t = Type::builtin(Type::Bases_Bool); break;
                case TT::KwChar: t = Type::builtin(Type::Bases_Char); break;
                case TT::KwInt: t = Type::builtin(Type::Bases_Int); break;
                case TT::KwFloat: t = Type::builtin(Type::Bases_Float); break;
                case TT::KwDouble: t = Type::builtin(Type::Bases_Double); break;
                default: break;
            }
        }
        if (!t) t = Type::builtin(Type::Bases_Int);
        auto& b = std::get<Type::BuiltinData>(t->data);
        b.isUnsigned = isUnsigned;
        b.isShort = isShort;
        b.isLong = isLong;
        b.isConst |= isConst;
        b.isVolatile |= isVolatile;
        return t;
    }

    // class/struct/enum/typename keyword in type position
    if (peek().kind == TT::KwClass || peek().kind == TT::KwStruct ||
        peek().kind == TT::KwEnum || peek().kind == TT::KwUnion) {
        ++pos_;
    }
    if (peek().kind == TT::KwTypename) ++pos_;

    if (peek().kind != TT::Identifier) {
        error("expected a type");
    }
    std::string name = next().text;
    while (match(TT::ColonColon)) name += "::" + expectIdent().text;

    if (at(TT::Less)) {
        ++pos_;
        int depth = 1;
        while (depth > 0 && !at(TT::End)) {
            if (at(TT::Less)) ++depth;
            if (at(TT::Greater)) --depth;
            ++pos_;
        }
    }
    while (peek().kind == TT::KwConst || peek().kind == TT::KwVolatile) ++pos_;

    if (sawTypeName) *sawTypeName = true;
    return Type::named(name);
}

Parser::Declarator Parser::parseDeclarator(TypePtr base, bool requireName) {
    while (true) {
        if (match(TT::Star)) {
            auto wrapped = Type::pointer(base);
            if (match(TT::KwConst)) {
                std::get<Type::PointerData>(wrapped->data).isConst = true;
            }
            base = std::move(wrapped);
            continue;
        }
        if (match(TT::Amp)) {
            base = Type::reference(base);
            continue;
        }
        break;
    }

    Declarator dec;
    dec.type = base;

    if (requireName) {
        if (at(TT::KwOperator)) {
            dec.name = "operator";
            ++pos_;
            switch (peek().kind) {
                case TT::Lparen:
                    ++pos_;
                    expect(TT::Rparen);
                    dec.name += "()";
                    break;
                case TT::Lbrack:
                    ++pos_;
                    expect(TT::Rbrack);
                    dec.name += "[]";
                    break;
                case TT::KwNew:
                    ++pos_;
                    dec.name += " new";
                    break;
                case TT::KwDelete:
                    ++pos_;
                    dec.name += " delete";
                    break;
                default: {
                    std::string sym = operatorName(peek().kind);
                    if (sym.empty()) error("invalid operator");
                    dec.name += sym;
                    ++pos_;
                    break;
                }
            }
        } else {
            Token n = expectIdent();
            dec.name = n.text;
            while (match(TT::ColonColon)) dec.name += "::" + expectIdent().text;
        }
    }

    for (;;) {
        if (at(TT::Lbrack)) {
            ++pos_;
            std::optional<std::size_t> size;
            if (at(TT::IntLit)) {
                Token n = next();
                size = static_cast<std::size_t>(std::strtoull(n.text.c_str(), nullptr, 0));
            } else if (at(TT::Identifier)) {
                ++pos_;  // unknown size (VLA-ish); treat as unsized
            }
            expect(TT::Rbrack);
            dec.type = Type::array(dec.type, size);
            continue;
        }
        break;
    }

    if (at(TT::Lparen)) {
        dec.hasFunctionParens = true;
        parseFunctionSuffix(base, dec.params, dec.isVarArg);
        dec.type = base;  // return type
    }

    return dec;
}

Parser::Declarator Parser::parseDeclaratorNoFunc(TypePtr base, bool requireName) {
    while (true) {
        if (match(TT::Star)) {
            auto wrapped = Type::pointer(base);
            if (match(TT::KwConst)) {
                std::get<Type::PointerData>(wrapped->data).isConst = true;
            }
            base = std::move(wrapped);
            continue;
        }
        if (match(TT::Amp)) {
            base = Type::reference(base);
            continue;
        }
        if (at(TT::Lbrack)) {
            ++pos_;
            std::optional<std::size_t> size;
            if (at(TT::IntLit)) {
                Token n = next();
                size = static_cast<std::size_t>(std::strtoull(n.text.c_str(), nullptr, 0));
            } else if (at(TT::Identifier)) {
                ++pos_;
            }
            expect(TT::Rbrack);
            base = Type::array(base, size);
            continue;
        }
        break;
    }
    Declarator d;
    d.type = base;
    if (requireName) {
        Token n = expectIdent();
        d.name = n.text;
    }
    return d;
}

void Parser::parseFunctionSuffix(TypePtr base, std::vector<ParamDecl>& params,
                                 bool& isVarArg) {
    (void)base;
    expect(TT::Lparen);
    if (match(TT::Rparen)) return;

    if (at(TT::Ellipsis)) {
        isVarArg = true;
        ++pos_;
        expect(TT::Rparen);
        return;
    }

    for (;;) {
        ParamDecl p;
        if (at(TT::KwVoid) && peek(1).kind == TT::Rparen) {
            ++pos_;
            expect(TT::Rparen);
            return;
        }
        TypePtr pt = parseTypeSpec();
        if (at(TT::Comma) || at(TT::Rparen)) {
            p.type = pt;
        } else {
            Declarator pd = parseDeclarator(pt, true);
            p.type = pd.type;
            p.name = pd.name;
        }
        if (match(TT::Assign)) p.defaultValue = parseConditional();
        params.push_back(std::move(p));
        if (!match(TT::Comma)) break;
        if (at(TT::Ellipsis)) {
            isVarArg = true;
            ++pos_;
            break;
        }
    }
    expect(TT::Rparen);
}

std::vector<ClassMember> Parser::parseClassBody(const std::string& className,
                                                bool isStruct, bool isUnion) {
    (void)isUnion;
    std::vector<ClassMember> members;
    bool isPrivate = !isStruct;  // class defaults to private, struct to public

    while (!at(TT::Rbrace) && !at(TT::End)) {
        TT k = peek().kind;

        if (k == TT::KwPublic || k == TT::KwPrivate || k == TT::KwProtected) {
            isPrivate = (k != TT::KwPublic);
            ++pos_;
            expect(TT::Colon);
            continue;
        }
        if (k == TT::Semicolon) {
            ++pos_;
            continue;
        }
        if (k == TT::KwFriend || k == TT::KwUsing) {
            while (!at(TT::Semicolon) && !at(TT::End) && !at(TT::Rbrace)) ++pos_;
            match(TT::Semicolon);
            continue;
        }
        if (k == TT::KwTemplate) {
            error("template declarations inside classes are not supported yet");
        }
        if (k == TT::KwClass || k == TT::KwStruct || k == TT::KwUnion) {
            DeclPtr nested = parseStructOrClass(k);
            ClassMember m;
            m.kind = ClassMember::Kind::NestedClass;
            m.nestedClass = nested->klass;
            m.isPrivate = isPrivate;
            members.push_back(std::move(m));
            continue;
        }
        if (k == TT::KwEnum) {
            DeclPtr nested = parseEnum();
            ClassMember m;
            m.kind = ClassMember::Kind::NestedEnum;
            m.nestedEnum = nested->enumm;
            m.isPrivate = isPrivate;
            members.push_back(std::move(m));
            continue;
        }

        SourceLoc loc = peek().loc;
        bool isStatic = false;
        bool isInline = false;
        bool isVirtual = false;
        while (isDeclSpecKeyword(peek().kind)) {
            TT sp = peek().kind;
            if (sp == TT::KwStatic) isStatic = true;
            if (sp == TT::KwInline) isInline = true;
            if (sp == TT::KwVirtual) isVirtual = true;
            ++pos_;
        }

        // constructors/destructors have no return type: `Led(int p)`, `~Led()`
        bool isCtorDecl = peek().kind == TT::Identifier && peek().text == className &&
                          peek(1).kind == TT::Lparen;
        bool isDtorDecl = peek().kind == TT::Tilde && peek(1).kind == TT::Identifier &&
                          peek(1).text == className && peek(2).kind == TT::Lparen;

        TypePtr type;
        Declarator dec;
        if (isCtorDecl || isDtorDecl) {
            if (isCtorDecl) {
                dec.name = className;
                ++pos_;  // className
            } else {
                ++pos_;  // ~
                dec.name = "~" + expectIdent().text;
                if (match(TT::ColonColon)) {}  // ~A::A()
            }
            type = Type::builtin(Type::Bases_Void);
        } else {
            type = parseTypeSpec();
            dec = parseDeclarator(type, true);
        }

        if (dec.hasFunctionParens || isCtorDecl || isDtorDecl) {
            if (!dec.hasFunctionParens) {
                // parse the parameter list for ctor/dtor
                dec.hasFunctionParens = true;
                parseFunctionSuffix(type, dec.params, dec.isVarArg);
                dec.type = type;
            }
            auto f = std::make_shared<FunctionDecl>();
            f->returns = dec.type;
            f->name = dec.name;
            f->params = std::move(dec.params);
            f->loc = loc;
            f->isStatic = isStatic;
            f->isInline = isInline;
            f->isVirtual = isVirtual;
            f->isConstructor = (dec.name == className);
            f->isDestructor = (!dec.name.empty() && dec.name[0] == '~');
            f->isOperator = (dec.name.rfind("operator", 0) == 0);
            if (match(TT::KwConst)) f->isConstMethod = true;
            if (match(TT::KwNoexcept)) {}
            // constructor init list: Foo() : a(1), b(2) { }
            if (match(TT::Colon)) {
                while (!at(TT::Lbrace) && !at(TT::Semicolon) && !at(TT::End)) {
                    if (at(TT::Comma) || at(TT::Colon)) {
                        ++pos_;
                        continue;
                    }
                    if (at(TT::Lparen)) {
                        ++pos_;
                        int depth = 1;
                        while (depth > 0 && !at(TT::End)) {
                            if (at(TT::Lparen)) ++depth;
                            if (at(TT::Rparen)) --depth;
                            ++pos_;
                        }
                        continue;
                    }
                    if (at(TT::Identifier)) ++pos_;
                    else ++pos_;
                }
                // 'match(Colon)' consumed colon; stop at { or ;
            }
            if (at(TT::Lbrace)) {
                f->body = parseBlock();
            } else {
                match(TT::Semicolon);
                if (match(TT::Assign)) {
                    // = 0 (pure virtual), = default, = delete
                    while (!at(TT::Semicolon) && !at(TT::End)) ++pos_;
                    match(TT::Semicolon);
                }
            }
            ClassMember m;
            m.kind = ClassMember::Kind::Method;
            m.method = std::move(f);
            m.isPrivate = isPrivate;
            members.push_back(std::move(m));
            continue;
        }

        // field(s)
        auto v = std::make_shared<VarDecl>();
        v->type = dec.type;
        v->name = dec.name;
        v->loc = loc;
        v->isStatic = isStatic;
        if (match(TT::Assign)) v->init = parseConditional();
        match(TT::Semicolon);
        ClassMember m;
        m.kind = ClassMember::Kind::Field;
        m.field = v;
        m.isPrivate = isPrivate;
        members.push_back(std::move(m));
    }
    return members;
}

std::vector<DeclPtr> Parser::parseSimpleDeclaration(bool requireSemicolon) {
    SourceLoc loc = peek().loc;
    std::vector<DeclPtr> out;

    bool isStatic = false, isExtern = false, isConstexpr = false;
    while (peek().kind == TT::KwStatic || peek().kind == TT::KwExtern ||
           peek().kind == TT::KwConstexpr || peek().kind == TT::KwInline ||
           peek().kind == TT::KwRegister || peek().kind == TT::KwMutable) {
        if (peek().kind == TT::KwStatic) isStatic = true;
        if (peek().kind == TT::KwExtern) isExtern = true;
        if (peek().kind == TT::KwConstexpr) isConstexpr = true;
        ++pos_;
    }

    TypePtr base = parseTypeSpec();

    for (;;) {
        Declarator dec = parseDeclarator(base, /*requireName=*/true);

        if (dec.hasFunctionParens) {
            auto f = std::make_shared<FunctionDecl>();
            f->returns = dec.type;
            f->name = dec.name;
            f->params = std::move(dec.params);
            f->loc = loc;
            f->isStatic = isStatic;
            if (match(TT::KwConst)) f->isConstMethod = true;
            if (match(TT::KwNoexcept)) {}

            // constructor init list for out-of-line definitions
            if (dec.name.find("::") != std::string::npos && match(TT::Colon)) {
                while (!at(TT::Lbrace) && !at(TT::Semicolon) && !at(TT::End)) {
                    if (at(TT::Lparen)) {
                        ++pos_;
                        int depth = 1;
                        while (depth > 0 && !at(TT::End)) {
                            if (at(TT::Lparen)) ++depth;
                            if (at(TT::Rparen)) --depth;
                            ++pos_;
                        }
                        continue;
                    }
                    ++pos_;
                }
            }

            if (at(TT::Lbrace)) {
                f->body = parseBlock();
                auto d = Decl::make(Decl::Kind::Function, loc);
                d->func = std::move(f);
                out.push_back(std::move(d));
                return out;
            }
            match(TT::Semicolon);
            auto d = Decl::make(Decl::Kind::Function, loc);
            d->func = std::move(f);
            out.push_back(std::move(d));
            if (!match(TT::Comma)) return out;
            continue;
        }

        auto v = std::make_shared<VarDecl>();
        v->type = dec.type;
        v->name = dec.name;
        v->loc = loc;
        v->isStatic = isStatic;
        v->isExtern = isExtern;
        v->isConstexpr = isConstexpr;
        if (match(TT::Assign)) v->init = parseConditional();

        auto d = Decl::make(Decl::Kind::Var, loc);
        d->var = v;
        out.push_back(std::move(d));

        if (!match(TT::Comma)) break;
    }

    if (requireSemicolon) expect(TT::Semicolon);
    return out;
}

// --------------------------------------------------------------------------
// Statements
// --------------------------------------------------------------------------

StmtPtr Parser::parseStatement() {
    SourceLoc loc = peek().loc;

    switch (peek().kind) {
        case TT::Lbrace: return parseBlock();
        case TT::Semicolon: {
            ++pos_;
            return Stmt::make(Stmt::Kind::Empty, loc);
        }
        case TT::KwIf: return parseIf();
        case TT::KwWhile: return parseWhile();
        case TT::KwDo: return parseDoWhile();
        case TT::KwFor: return parseFor();
        case TT::KwSwitch: return parseSwitch();
        case TT::KwBreak: {
            ++pos_;
            expect(TT::Semicolon);
            auto s = Stmt::make(Stmt::Kind::Break, loc);
            return s;
        }
        case TT::KwContinue: {
            ++pos_;
            expect(TT::Semicolon);
            auto s = Stmt::make(Stmt::Kind::Continue, loc);
            return s;
        }
        case TT::KwGoto: {
            ++pos_;
            Token n = expectIdent();
            expect(TT::Semicolon);
            auto s = Stmt::make(Stmt::Kind::Goto, loc);
            s->name = n.text;
            return s;
        }
        case TT::KwReturn: {
            ++pos_;
            auto s = Stmt::make(Stmt::Kind::Return, loc);
            if (!at(TT::Semicolon)) s->expr = parseExpr();
            expect(TT::Semicolon);
            return s;
        }
        case TT::KwCase:
        case TT::KwDefault:
            return parseLabeled();
        default:
            break;
    }

    if (peek().kind == TT::Identifier && peek(1).kind == TT::Colon) {
        Token n = next();
        ++pos_;  // ':'
        auto s = Stmt::make(Stmt::Kind::Label, loc);
        s->name = n.text;
        s->body = parseStatement();
        return s;
    }

    if (looksLikeDeclarationStart()) {
        SourceLoc dloc = peek().loc;
        std::vector<DeclPtr> decls = parseSimpleDeclaration(true);
        if (decls.size() == 1) {
            auto s = Stmt::make(Stmt::Kind::Declaration, loc);
            if (decls[0]->kind == Decl::Kind::Var) s->decl = decls[0]->var;
            return s;
        }
        auto block = Stmt::make(Stmt::Kind::Block, dloc);
        for (auto& d : decls) {
            auto s = Stmt::make(Stmt::Kind::Declaration, d->loc);
            if (d->kind == Decl::Kind::Var) s->decl = d->var;
            block->items.push_back(std::move(s));
        }
        return block;
    }

    auto s = Stmt::make(Stmt::Kind::Expr, loc);
    s->expr = parseExpr();
    expect(TT::Semicolon);
    return s;
}

StmtPtr Parser::parseBlock() {
    SourceLoc loc = peek().loc;
    expect(TT::Lbrace);
    auto s = Stmt::make(Stmt::Kind::Block, loc);
    while (!at(TT::Rbrace) && !at(TT::End)) s->items.push_back(parseStatement());
    expect(TT::Rbrace);
    return s;
}

StmtPtr Parser::parseIf() {
    SourceLoc loc = peek().loc;
    ++pos_;
    expect(TT::Lparen);
    auto s = Stmt::make(Stmt::Kind::If, loc);
    s->cond = parseExpr();
    expect(TT::Rparen);
    s->body = parseStatement();
    if (match(TT::KwElse)) s->elseBody = parseStatement();
    return s;
}

StmtPtr Parser::parseWhile() {
    SourceLoc loc = peek().loc;
    ++pos_;
    expect(TT::Lparen);
    auto s = Stmt::make(Stmt::Kind::While, loc);
    s->cond = parseExpr();
    expect(TT::Rparen);
    s->body = parseStatement();
    return s;
}

StmtPtr Parser::parseDoWhile() {
    SourceLoc loc = peek().loc;
    ++pos_;
    auto s = Stmt::make(Stmt::Kind::DoWhile, loc);
    s->body = parseStatement();
    if (match(TT::KwWhile)) {
        expect(TT::Lparen);
        s->cond = parseExpr();
        expect(TT::Rparen);
    }
    match(TT::Semicolon);
    return s;
}

StmtPtr Parser::parseFor() {
    SourceLoc loc = peek().loc;
    ++pos_;
    expect(TT::Lparen);
    auto s = Stmt::make(Stmt::Kind::For, loc);

    if (!at(TT::Semicolon)) {
        if (looksLikeDeclarationStart()) {
            std::vector<DeclPtr> decls = parseSimpleDeclaration(false);
            s->initStmt = Stmt::make(Stmt::Kind::Block, loc);
            for (auto& d : decls) {
                auto inner = Stmt::make(Stmt::Kind::Declaration, d->loc);
                if (d->kind == Decl::Kind::Var) inner->decl = d->var;
                s->initStmt->items.push_back(std::move(inner));
            }
        } else {
            auto e = Stmt::make(Stmt::Kind::Expr, loc);
            e->expr = parseExpr();
            s->initStmt = std::move(e);
        }
    }
    expect(TT::Semicolon);

    if (!at(TT::Semicolon)) s->cond = parseExpr();
    expect(TT::Semicolon);

    if (!at(TT::Rparen)) s->thenExpr = parseExpr();
    expect(TT::Rparen);

    s->body = parseStatement();
    return s;
}

StmtPtr Parser::parseSwitch() {
    SourceLoc loc = peek().loc;
    ++pos_;
    expect(TT::Lparen);
    auto s = Stmt::make(Stmt::Kind::Switch, loc);
    s->cond = parseExpr();
    expect(TT::Rparen);
    s->body = parseStatement();
    return s;
}

StmtPtr Parser::parseLabeled() {
    SourceLoc loc = peek().loc;
    if (at(TT::KwCase)) {
        ++pos_;
        auto s = Stmt::make(Stmt::Kind::Case, loc);
        s->expr = parseConditional();
        expect(TT::Colon);
        s->body = parseStatement();
        return s;
    }
    ++pos_;  // default
    expect(TT::Colon);
    auto s = Stmt::make(Stmt::Kind::Default, loc);
    s->body = parseStatement();
    return s;
}

bool Parser::looksLikeDeclarationStart() const {
    TT k = peek().kind;
    if (isBuiltinTypeToken(k) || isDeclSpecKeyword(k)) return true;
    if (k == TT::KwClass || k == TT::KwStruct || k == TT::KwEnum) return true;

    if (k == TT::Identifier) {
        TT n = peek(1).kind;
        if (n == TT::Identifier) return true;
        if (n == TT::Star || n == TT::Amp) return true;
        if (n == TT::ColonColon) {
            // qualified name: scan past `::Id` chain
            std::size_t i = 1;
            while (i + 1 < toks_.size() &&
                   toks_[pos_ + i].kind == TT::ColonColon &&
                   toks_[pos_ + i + 1].kind == TT::Identifier) {
                i += 2;
            }
            TT after = peek(i).kind;
            if (after == TT::Lparen) return false;  // Foo::bar() is a call
            return after == TT::Identifier || after == TT::Star || after == TT::Amp;
        }
        return false;
    }
    return false;
}

// --------------------------------------------------------------------------
// Expressions
// --------------------------------------------------------------------------

ExprPtr Parser::parseExpr() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseAssignment();
    if (at(TT::Comma)) {
        auto c = Expr::make(Expr::Kind::Comma, loc);
        c->lhs = std::move(e);
        while (match(TT::Comma)) c->args.push_back(parseAssignment());
        return c;
    }
    return e;
}

ExprPtr Parser::parseAssignment() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseConditional();
    AssignOp op;
    switch (peek().kind) {
        case TT::Assign: op = AssignOp::Assign; break;
        case TT::PlusEq: op = AssignOp::Add; break;
        case TT::MinusEq: op = AssignOp::Sub; break;
        case TT::StarEq: op = AssignOp::Mul; break;
        case TT::SlashEq: op = AssignOp::Div; break;
        case TT::PercentEq: op = AssignOp::Mod; break;
        case TT::ShiftLeftEq: op = AssignOp::Shl; break;
        case TT::ShiftRightEq: op = AssignOp::Shr; break;
        case TT::AmpEq: op = AssignOp::BitAnd; break;
        case TT::CaretEq: op = AssignOp::BitXor; break;
        case TT::PipeEq: op = AssignOp::BitOr; break;
        default: return e;
    }
    ++pos_;
    auto a = Expr::make(Expr::Kind::Assign, loc);
    a->assignOp = op;
    a->lhs = std::move(e);
    a->rhs = parseAssignment();  // right associative
    return a;
}

ExprPtr Parser::parseConditional() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseLogicalOr();
    if (match(TT::Question)) {
        auto c = Expr::make(Expr::Kind::Conditional, loc);
        c->cond = std::move(e);
        c->thenExpr = parseExpr();
        expect(TT::Colon);
        c->elseExpr = parseConditional();
        return c;
    }
    return e;
}

ExprPtr Parser::parseLogicalOr() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseLogicalAnd();
    while (at(TT::PipePipe)) {
        ++pos_;
        auto b = Expr::make(Expr::Kind::Binary, loc);
        b->binOp = BinOp::LOr;
        b->lhs = std::move(e);
        b->rhs = parseLogicalAnd();
        e = std::move(b);
        loc = peek().loc;
    }
    return e;
}

ExprPtr Parser::parseLogicalAnd() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseBitOr();
    while (at(TT::AmpAmp)) {
        ++pos_;
        auto b = Expr::make(Expr::Kind::Binary, loc);
        b->binOp = BinOp::LAnd;
        b->lhs = std::move(e);
        b->rhs = parseBitOr();
        e = std::move(b);
        loc = peek().loc;
    }
    return e;
}

ExprPtr Parser::parseBitOr() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseBitXor();
    while (at(TT::Pipe)) {
        ++pos_;
        auto b = Expr::make(Expr::Kind::Binary, loc);
        b->binOp = BinOp::BitOr;
        b->lhs = std::move(e);
        b->rhs = parseBitXor();
        e = std::move(b);
        loc = peek().loc;
    }
    return e;
}

ExprPtr Parser::parseBitXor() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseBitAnd();
    while (at(TT::Caret)) {
        ++pos_;
        auto b = Expr::make(Expr::Kind::Binary, loc);
        b->binOp = BinOp::BitXor;
        b->lhs = std::move(e);
        b->rhs = parseBitAnd();
        e = std::move(b);
        loc = peek().loc;
    }
    return e;
}

ExprPtr Parser::parseBitAnd() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseEquality();
    while (at(TT::Amp)) {
        ++pos_;
        auto b = Expr::make(Expr::Kind::Binary, loc);
        b->binOp = BinOp::BitAnd;
        b->lhs = std::move(e);
        b->rhs = parseEquality();
        e = std::move(b);
        loc = peek().loc;
    }
    return e;
}

ExprPtr Parser::parseEquality() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseRelational();
    for (;;) {
        BinOp op;
        if (at(TT::EqEq)) op = BinOp::Eq;
        else if (at(TT::NotEq)) op = BinOp::Ne;
        else break;
        ++pos_;
        auto b = Expr::make(Expr::Kind::Binary, loc);
        b->binOp = op;
        b->lhs = std::move(e);
        b->rhs = parseRelational();
        e = std::move(b);
        loc = peek().loc;
    }
    return e;
}

ExprPtr Parser::parseRelational() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseShift();
    for (;;) {
        BinOp op;
        if (at(TT::Less)) op = BinOp::Lt;
        else if (at(TT::Greater)) op = BinOp::Gt;
        else if (at(TT::LessEq)) op = BinOp::Le;
        else if (at(TT::GreaterEq)) op = BinOp::Ge;
        else break;
        ++pos_;
        auto b = Expr::make(Expr::Kind::Binary, loc);
        b->binOp = op;
        b->lhs = std::move(e);
        b->rhs = parseShift();
        e = std::move(b);
        loc = peek().loc;
    }
    return e;
}

ExprPtr Parser::parseShift() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseAdditive();
    for (;;) {
        BinOp op;
        if (at(TT::ShiftLeft)) op = BinOp::Shl;
        else if (at(TT::ShiftRight)) op = BinOp::Shr;
        else break;
        ++pos_;
        auto b = Expr::make(Expr::Kind::Binary, loc);
        b->binOp = op;
        b->lhs = std::move(e);
        b->rhs = parseAdditive();
        e = std::move(b);
        loc = peek().loc;
    }
    return e;
}

ExprPtr Parser::parseAdditive() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseMultiplicative();
    for (;;) {
        BinOp op;
        if (at(TT::Plus)) op = BinOp::Add;
        else if (at(TT::Minus)) op = BinOp::Sub;
        else break;
        ++pos_;
        auto b = Expr::make(Expr::Kind::Binary, loc);
        b->binOp = op;
        b->lhs = std::move(e);
        b->rhs = parseMultiplicative();
        e = std::move(b);
        loc = peek().loc;
    }
    return e;
}

ExprPtr Parser::parseMultiplicative() {
    SourceLoc loc = peek().loc;
    ExprPtr e = parseUnary();
    for (;;) {
        BinOp op;
        if (at(TT::Star)) op = BinOp::Mul;
        else if (at(TT::Slash)) op = BinOp::Div;
        else if (at(TT::Percent)) op = BinOp::Mod;
        else break;
        ++pos_;
        auto b = Expr::make(Expr::Kind::Binary, loc);
        b->binOp = op;
        b->lhs = std::move(e);
        b->rhs = parseUnary();
        e = std::move(b);
        loc = peek().loc;
    }
    return e;
}

ExprPtr Parser::parseUnary() {
    SourceLoc loc = peek().loc;

    if (at(TT::Bang)) {
        ++pos_;
        auto u = Expr::make(Expr::Kind::Unary, loc);
        u->unOp = UnOp::Not;
        u->operand = parseUnary();
        return u;
    }
    if (at(TT::Tilde)) {
        ++pos_;
        auto u = Expr::make(Expr::Kind::Unary, loc);
        u->unOp = UnOp::BitNot;
        u->operand = parseUnary();
        return u;
    }
    if (at(TT::Plus)) {
        ++pos_;
        auto u = Expr::make(Expr::Kind::Unary, loc);
        u->unOp = UnOp::Plus;
        u->operand = parseUnary();
        return u;
    }
    if (at(TT::Minus)) {
        ++pos_;
        auto u = Expr::make(Expr::Kind::Unary, loc);
        u->unOp = UnOp::Neg;
        u->operand = parseUnary();
        return u;
    }
    if (at(TT::Star)) {
        ++pos_;
        auto u = Expr::make(Expr::Kind::Unary, loc);
        u->unOp = UnOp::Deref;
        u->operand = parseUnary();
        return u;
    }
    if (at(TT::Amp)) {
        ++pos_;
        auto u = Expr::make(Expr::Kind::Unary, loc);
        u->unOp = UnOp::AddrOf;
        u->operand = parseUnary();
        return u;
    }
    if (at(TT::PlusPlus)) {
        ++pos_;
        auto u = Expr::make(Expr::Kind::Unary, loc);
        u->unOp = UnOp::PreInc;
        u->operand = parseUnary();
        return u;
    }
    if (at(TT::MinusMinus)) {
        ++pos_;
        auto u = Expr::make(Expr::Kind::Unary, loc);
        u->unOp = UnOp::PreDec;
        u->operand = parseUnary();
        return u;
    }
    if (at(TT::KwSizeof)) {
        ++pos_;
        SourceLoc sl = peek().loc;
        if (match(TT::Lparen)) {
            std::size_t save = pos_;
            TypePtr t;
            if (tryParseParenAsType(t)) {
                auto s = Expr::make(Expr::Kind::SizeOf, sl);
                s->type = t;
                return s;
            }
            pos_ = save;
            auto s = Expr::make(Expr::Kind::SizeOf, sl);
            s->operand = parseExpr();
            expect(TT::Rparen);
            return s;
        }
        auto s = Expr::make(Expr::Kind::SizeOf, sl);
        s->operand = parseUnary();
        return s;
    }
    if (at(TT::KwNew)) return parseNewExpression();
    if (at(TT::KwDelete)) return parseDeleteExpression();
    if (at(TT::KwStaticCast) || at(TT::KwReinterpretCast) || at(TT::KwConstCast)) {
        CastKind ck;
        if (at(TT::KwStaticCast)) ck = CastKind::Static;
        else if (at(TT::KwReinterpretCast)) ck = CastKind::Reinterpret;
        else ck = CastKind::Const;
        ++pos_;
        expect(TT::Less);
        TypePtr t = parseTypeSpec();
        expect(TT::Greater);
        expect(TT::Lparen);
        auto c = Expr::make(Expr::Kind::Cast, loc);
        c->castKind = ck;
        c->type = t;
        c->operand = parseExpr();
        expect(TT::Rparen);
        return c;
    }

    if (at(TT::Lparen)) {
        ExprPtr cast = tryParseCastExpression();
        if (cast) return cast;
    }

    return parsePostfix();
}

ExprPtr Parser::parseNewExpression() {
    SourceLoc loc = peek().loc;
    ++pos_;  // new

    // placement new: new(pool) T(...) - consume balanced parens
    if (at(TT::Lparen)) {
        ++pos_;
        int depth = 1;
        while (depth > 0 && !at(TT::End)) {
            if (at(TT::Lparen)) ++depth;
            if (at(TT::Rparen)) --depth;
            ++pos_;
        }
    }

    auto e = Expr::make(Expr::Kind::NewExpr, loc);
    bool isArray = false;
    if (match(TT::Lbrack)) {
        isArray = true;
        expect(TT::Rbrack);
    }

    TypePtr base = parseTypeSpec();
    if (at(TT::Lparen)) {
        ++pos_;
        while (!at(TT::Rparen) && !at(TT::End)) {
            e->args.push_back(parseConditional());
            if (!match(TT::Comma)) break;
        }
        expect(TT::Rparen);
        e->type = base;
        return e;
    }
    if (at(TT::Lbrack)) {
        ++pos_;
        e->newCount = parseConditional();
        expect(TT::Rbrack);
        e->isArrayNew = true;
        e->type = base;
        return e;
    }
    e->type = base;
    e->isArrayNew = isArray;
    if (isArray && !e->newCount) {
        e->newCount = Expr::make(Expr::Kind::IntLit, loc);
        e->newCount->intValue = 1;
    }
    return e;
}

ExprPtr Parser::parseDeleteExpression() {
    SourceLoc loc = peek().loc;
    ++pos_;
    auto e = Expr::make(Expr::Kind::DeleteExpr, loc);
    if (match(TT::Lbrack)) {
        expect(TT::Rbrack);
        e->isArrayDelete = true;
    }
    e->operand = parseUnary();
    return e;
}

ExprPtr Parser::tryParseCastExpression() {
    std::size_t save = pos_;
    SourceLoc loc = peek().loc;
    ++pos_;  // '('
    TypePtr t;
    if (!tryParseParenAsType(t)) {
        pos_ = save;
        return nullptr;
    }
    auto c = Expr::make(Expr::Kind::Cast, loc);
    c->castKind = CastKind::C;
    c->type = t;
    c->operand = parseUnary();
    return c;
}

// Attempts to parse `( Type )` with '(' already consumed.
// On success stream is positioned after ')'.
bool Parser::tryParseParenAsType(TypePtr& outType) {
    std::size_t save = pos_;
    try {
        TypePtr t = parseTypeSpec();
        if (at(TT::Rparen)) {
            ++pos_;
            outType = t;
            return true;
        }
        if (at(TT::Star) || at(TT::Amp)) {
            Declarator d = parseDeclaratorNoFunc(t, false);
            if (at(TT::Rparen)) {
                ++pos_;
                outType = d.type;
                return true;
            }
        }
    } catch (const ParseError&) {
    }
    pos_ = save;
    return false;
}

ExprPtr Parser::parsePostfix() {
    ExprPtr e = parsePrimary();
    for (;;) {
        TT k = peek().kind;
        if (k == TT::Lparen || k == TT::Lbrack || k == TT::Dot ||
            k == TT::Arrow || k == TT::PlusPlus || k == TT::MinusMinus ||
            k == TT::ColonColon) {
            e = parsePostfixSuffix(std::move(e));
        } else {
            break;
        }
    }
    return e;
}

ExprPtr Parser::parsePostfixSuffix(ExprPtr base) {
    SourceLoc loc = peek().loc;
    switch (peek().kind) {
        case TT::Lparen: {
            ++pos_;
            auto c = Expr::make(Expr::Kind::Call, loc);
            c->object = std::move(base);
            while (!at(TT::Rparen) && !at(TT::End)) {
                c->args.push_back(parseConditional());
                if (!match(TT::Comma)) break;
            }
            expect(TT::Rparen);
            return c;
        }
        case TT::Lbrack: {
            ++pos_;
            auto i = Expr::make(Expr::Kind::Index, loc);
            i->object = std::move(base);
            i->operand = parseExpr();
            expect(TT::Rbrack);
            return i;
        }
        case TT::Dot:
        case TT::Arrow: {
            bool arrow = peek().kind == TT::Arrow;
            ++pos_;
            if (match(TT::Tilde)) {
                auto m = Expr::make(Expr::Kind::Member, loc);
                m->object = std::move(base);
                m->arrowAccess = arrow;
                m->member = "~" + expectIdent().text;
                return m;
            }
            auto m = Expr::make(Expr::Kind::Member, loc);
            m->object = std::move(base);
            m->arrowAccess = arrow;
            m->member = expectIdent().text;
            return m;
        }
        case TT::PlusPlus:
        case TT::MinusMinus: {
            TT op = peek().kind;
            ++pos_;
            auto u = Expr::make(Expr::Kind::Unary, loc);
            u->unOp = (op == TT::PlusPlus) ? UnOp::PostInc : UnOp::PostDec;
            u->operand = std::move(base);
            return u;
        }
        case TT::ColonColon: {
            ++pos_;
            auto m = Expr::make(Expr::Kind::Member, loc);
            m->object = std::move(base);
            m->member = expectIdent().text;
            return m;
        }
        default:
            error("invalid postfix expression");
    }
}

ExprPtr Parser::parsePrimary() {
    SourceLoc loc = peek().loc;
    TT k = peek().kind;

    switch (k) {
        case TT::IntLit:
        case TT::FloatLit: {
            const Token& t = next();
            if (t.kind == TT::IntLit) {
                auto e = Expr::make(Expr::Kind::IntLit, loc);
                std::string s;
                for (char c : t.text)
                    if (c != '\'') s += c;  // drop digit separators
                int base = 10;
                std::size_t start = 0;
                if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                    base = 16;
                    start = 2;
                } else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
                    base = 2;
                    start = 2;
                } else if (s.size() > 1 && s[0] == '0') {
                    base = 8;
                    start = 1;
                } else {
                    base = 10;
                }
                std::string digits;
                for (std::size_t i = start; i < s.size(); ++i) {
                    char c = s[i];
                    bool ok = base == 16
                                  ? std::isxdigit(static_cast<unsigned char>(c))
                                  : (base == 8 ? (c >= '0' && c <= '7')
                                               : (base == 2 ? (c == '0' || c == '1')
                                                            : std::isdigit(static_cast<unsigned char>(c))));
                    if (!ok) break;
                    digits += c;
                }
                e->intValue = static_cast<unsigned long long>(
                    std::strtoull(digits.empty() ? "0" : digits.c_str(), nullptr, base));
                return e;
            }
            auto e = Expr::make(Expr::Kind::FloatLit, loc);
            std::string s;
            for (char c : t.text)
                if (c != '\'') s += c;
            while (!s.empty()) {
                char c = s.back();
                if (c == 'f' || c == 'F' || c == 'l' || c == 'L' || c == 'u' || c == 'U')
                    s.pop_back();
                else
                    break;
            }
            e->floatValue = std::strtod(s.c_str(), nullptr);
            return e;
        }
        case TT::StringLit: {
            const Token& t = next();
            auto e = Expr::make(Expr::Kind::StrLit, loc);
            e->stringValue = t.value;
            return e;
        }
        case TT::CharLit: {
            const Token& t = next();
            auto e = Expr::make(Expr::Kind::IntLit, loc);
            if (!t.value.empty())
                e->intValue = static_cast<unsigned char>(t.value[0]);
            return e;
        }
        case TT::KwTrue:
            ++pos_;
            {
                auto e = Expr::make(Expr::Kind::BoolLit, loc);
                e->boolValue = true;
                return e;
            }
        case TT::KwFalse:
            ++pos_;
            {
                auto e = Expr::make(Expr::Kind::BoolLit, loc);
                e->boolValue = false;
                return e;
            }
        case TT::KwNullptr:
            ++pos_;
            return Expr::make(Expr::Kind::NullLit, loc);
        case TT::KwThis:
            ++pos_;
            return Expr::make(Expr::Kind::This, loc);
        case TT::Identifier: {
            const Token& t = next();
            auto e = Expr::make(Expr::Kind::Identifier, loc);
            e->name = t.text;
            return e;
        }
        case TT::Lparen: {
            ++pos_;
            ExprPtr e = parseExpr();
            expect(TT::Rparen);
            return e;
        }
        default:
            error("expected expression");
    }
}

}  // namespace cppic