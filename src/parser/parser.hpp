#pragma once

#include "ast/ast.hpp"
#include "lexer/token.hpp"

#include <string>
#include <vector>

namespace cppic {

struct ParseError {
    std::string message;
    SourceLoc loc;
};

// Recursive-descent parser for the supported C++ subset.
// Consumes the full token stream (which must end with TT::End)
// and produces a TranslationUnit.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    TranslationUnit parseTranslationUnit();

private:
    // --- token helpers ---
    const Token& peek(std::size_t ahead = 0) const;
    bool at(TT kind) const;
    bool atIdent(const char* text) const;
    const Token& next();
    bool match(TT kind);
    void expect(TT kind);
    Token expectIdent();
    [[noreturn]] void error(TT kind);
    [[noreturn]] void error(const std::string& msg);

    // --- declarations ---
    std::vector<DeclPtr> parseTopLevel();
    DeclPtr parseStructOrClass(TT keywordTok);
    DeclPtr parseEnum();
    DeclPtr parseTypedef();
    std::vector<DeclPtr> parseUsing();
    std::vector<DeclPtr> parseSimpleDeclaration(bool requireSemicolon = true);
    std::vector<ClassMember> parseClassBody(const std::string& className,
                                            bool isStruct, bool isUnion);

    // --- types ---
    // Returns base type after decl-specifiers (e.g. const unsigned int)
    TypePtr parseTypeSpec(bool* sawTypeName = nullptr);
    struct Declarator {
        TypePtr type;
        std::string name;
        bool hasFunctionParens = false;
        std::vector<ParamDecl> params;
        bool isVarArg = false;
    };
    Declarator parseDeclarator(TypePtr base, bool requireName);
    Declarator parseDeclaratorNoFunc(TypePtr base, bool requireName);
    void parseFunctionSuffix(TypePtr base, std::vector<ParamDecl>& params,
                             bool& isVarArg);

    // --- statements ---
    StmtPtr parseStatement();
    StmtPtr parseBlock();
    StmtPtr parseIf();
    StmtPtr parseWhile();
    StmtPtr parseDoWhile();
    StmtPtr parseFor();
    StmtPtr parseSwitch();
    StmtPtr parseLabeled();

    // --- expressions ---
    ExprPtr parseExpr();          // comma
    ExprPtr parseAssignment();    // assignment (right associative)
    ExprPtr parseConditional();
    ExprPtr parseLogicalOr();
    ExprPtr parseLogicalAnd();
    ExprPtr parseBitOr();
    ExprPtr parseBitXor();
    ExprPtr parseBitAnd();
    ExprPtr parseEquality();
    ExprPtr parseRelational();
    ExprPtr parseShift();
    ExprPtr parseAdditive();
    ExprPtr parseMultiplicative();
    ExprPtr parseUnary();
    ExprPtr parsePostfix();
    ExprPtr parsePrimary();
    ExprPtr parsePostfixSuffix(ExprPtr base);
    ExprPtr parseNewExpression();
    ExprPtr parseDeleteExpression();
    ExprPtr tryParseCastExpression();
    bool tryParseParenAsType(TypePtr& outType);

    bool looksLikeDeclarationStart() const;

    std::vector<Token> toks_;
    std::size_t pos_ = 0;
};

}  // namespace cppic