#pragma once

#include <cstddef>
#include <string>

namespace cppic {

enum class TT {
    End,

    // Identifiers and literals
    Identifier,
    IntLit,
    FloatLit,
    CharLit,
    StringLit,

    // Single-character punctuation
    Lbrack, Rbrack,      // [ ]
    Lparen, Rparen,      // ( )
    Lbrace, Rbrace,      // { }
    Semicolon,           // ;
    Colon,               // :
    Comma,               // ,
    Dot,                 // .
    Question,            // ?
    Tilde,               // ~
    Bang,                // !
    Plus, Minus,         // + -
    Star, Slash,         // * /
    Percent,             // %
    Amp, Pipe, Caret,    // & | ^
    Less, Greater,       // < >
    Assign,              // =

    // Multi-character operators
    Arrow,               // ->
    DotStar,             // .*
    ArrowStar,           // ->*
    PlusPlus,            // ++
    MinusMinus,          // --
    ShiftLeft,           // <<
    ShiftRight,          // >>
    ShiftLeftEq,         // <<=
    ShiftRightEq,        // >>=
    EqEq,                // ==
    NotEq,               // !=
    LessEq,              // <=
    GreaterEq,           // >=
    AmpAmp,              // &&
    PipePipe,            // ||
    PlusEq, MinusEq,     // += -=
    StarEq, SlashEq,     // *= /=
    PercentEq,           // %=
    AmpEq, PipeEq,       // &= |=
    CaretEq,             // ^=
    ColonColon,          // ::
    Ellipsis,            // ...

    // Keywords
    KwAlignas, KwAlignof, KwAsm, KwAuto, KwBool, KwBreak, KwCase, KwCatch,
    KwChar, KwClass, KwConst, KwConstexpr, KwConstCast, KwContinue, KwDefault,
    KwDelete, KwDo, KwDouble, KwDynamicCast, KwElse, KwEnum, KwExplicit,
    KwExport, KwExtern, KwFalse, KwFloat, KwFor, KwFriend, KwGoto, KwIf,
    KwInline, KwInt, KwLong, KwMutable, KwNamespace, KwNew, KwNoexcept,
    KwNullptr, KwOperator, KwPrivate, KwProtected, KwPublic, KwRegister,
    KwReinterpretCast, KwReturn, KwShort, KwSigned, KwSizeof, KwStatic,
    KwStaticAssert, KwStaticCast, KwStruct, KwSwitch, KwTemplate, KwThis,
    KwThrow, KwTrue, KwTry, KwTypedef, KwTypeid, KwTypename, KwUnion,
    KwUnsigned, KwUsing, KwVirtual, KwVoid, KwVolatile, KwWhile,
};

struct SourceLoc {
    int line = 0;
    int col = 0;
    std::size_t offset = 0;
};

struct Token {
    TT kind = TT::End;
    std::string text;   // raw lexeme as written in source
    std::string value;  // decoded value (identifiers / literals)
    SourceLoc loc;
};

const char* tokenName(TT kind);

}  // namespace cppic