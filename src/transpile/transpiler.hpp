#pragma once

#include "ast/ast.hpp"

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace cppic {

struct TranspileError {
    std::string message;
    SourceLoc loc;
};

// Lowering pass: C++ subset -> plain C (for SDCC / PIC18).
// Performs name mangling, resolves methods/fields, rewrites `this`,
// and turns class methods and operator overloads into free functions.
class Transpiler {
public:
    std::string run(const TranslationUnit& tu);

private:
    struct ClassInfo {
        std::string name;
        std::string cname;
        bool isStruct = false;
        bool isUnion = false;
        bool hasUserCtor = false;
        bool hasDefaultCtor = false;
        bool baseFieldsResolved = false;
        const FunctionDecl* dtor = nullptr;
        std::vector<std::string> bases;
        std::vector<const VarDecl*> staticFields;
        std::vector<const VarDecl*> instanceFields;  // own + flattened bases
        std::unordered_map<std::string, const VarDecl*> fieldByName;
        // method name / operator -> overloads
        std::unordered_map<std::string, std::vector<const FunctionDecl*>> methods;
        std::vector<const FunctionDecl*> ctorOverloads;
    };

    struct Val {
        std::string code;
        TypePtr type;
        bool ptr = false;  // code denotes a pointer to `type`
    };

    struct Scope {
        std::unordered_map<std::string, TypePtr> vars;
    };

    // --- collection ---
    void collect(const TranslationUnit& tu);
    void collectClass(const std::shared_ptr<ClassDecl>& c);
    void addInstanceFields(ClassInfo& ci);

    // --- name mangling ---
    static std::string mangleName(const std::string& n);
    static std::string mangleOperator(const std::string& opName);
    std::string mangleFunction(const FunctionDecl& f, const ClassInfo* ci);
    std::string classCName(const std::string& name) const;

    // --- type helpers ---
    std::string typeToC(const Type& t) const;
    bool isClassType(const TypePtr& t) const;
    const ClassInfo* classOf(const TypePtr& t) const;
    TypePtr stripRef(const TypePtr& t) const;
    TypePtr derefType(const TypePtr& t) const;

    // --- expression emission ---
    Val emitExpr(const Expr& e);
    Val emitCall(const Expr& e);
    Val emitMember(const Expr& e, bool wantCall);
    Val emitBinary(const Expr& e);
    Val emitUnary(const Expr& e);
    std::string emitArgsAsValues(const std::vector<ExprPtr>& args,
                                 const std::vector<ParamDecl>* params,
                                 const std::string& receiverPrefix);

    // --- statement emission ---
    void emitStmt(const Stmt& s, int indent);
    void emitBlock(const Stmt& s, int indent);
    void emitDeclaration(const VarDecl& v, int indent);
    void emitLocalAssignCtor(const Val& target, const Expr& init, int indent);

    // --- declaration emission ---
    void emitPrototype(const FunctionDecl& f, const ClassInfo* ci);
    void emitFunction(const FunctionDecl& f, const ClassInfo* ci);
    void emitClassStruct(const ClassInfo& ci);
    void emitGlobalVar(const VarDecl& v);

    // --- scope ---
    void pushScope();
    void popScope();
    void declareVar(const std::string& name, TypePtr t);
    TypePtr lookupVar(const std::string& name) const;

    std::string indentStr(int n) const;
    [[noreturn]] void err(const std::string& msg, SourceLoc loc);

    // state
    std::unordered_map<std::string, ClassInfo> classes_;
    std::unordered_map<std::string, const VarDecl*> globals_;
    std::unordered_map<std::string, std::vector<const FunctionDecl*>> freeFuncs_;
    std::unordered_map<std::string, long long> enumConsts_;
    std::unordered_map<std::string, TypePtr> typedefs_;

    std::vector<Scope> scopes_;
    const ClassInfo* currentClass_ = nullptr;
    TypePtr currentReturns_;
    std::vector<std::string>* currentEmittingMethods_ = nullptr;

    std::ostringstream out_;
};

}  // namespace cppic