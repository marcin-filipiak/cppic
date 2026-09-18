#include "lexer/token.hpp"

namespace cppic {

#define ET(x) case TT::x: return #x

const char* tokenName(TT kind) {
    switch (kind) {
        ET(End);
        ET(Identifier);
        ET(IntLit);
        ET(FloatLit);
        ET(CharLit);
        ET(StringLit);
        ET(Lbrack); ET(Rbrack);
        ET(Lparen); ET(Rparen);
        ET(Lbrace); ET(Rbrace);
        ET(Semicolon); ET(Colon); ET(Comma); ET(Dot); ET(Question);
        ET(Tilde); ET(Bang);
        ET(Plus); ET(Minus); ET(Star); ET(Slash); ET(Percent);
        ET(Amp); ET(Pipe); ET(Caret); ET(Less); ET(Greater); ET(Assign);
        ET(Arrow); ET(DotStar); ET(ArrowStar);
        ET(PlusPlus); ET(MinusMinus);
        ET(ShiftLeft); ET(ShiftRight); ET(ShiftLeftEq); ET(ShiftRightEq);
        ET(EqEq); ET(NotEq); ET(LessEq); ET(GreaterEq);
        ET(AmpAmp); ET(PipePipe);
        ET(PlusEq); ET(MinusEq); ET(StarEq); ET(SlashEq); ET(PercentEq);
        ET(AmpEq); ET(PipeEq); ET(CaretEq);
        ET(ColonColon); ET(Ellipsis);
        ET(KwAlignas); ET(KwAlignof); ET(KwAsm); ET(KwAuto); ET(KwBool);
        ET(KwBreak); ET(KwCase); ET(KwCatch); ET(KwChar); ET(KwClass);
        ET(KwConst); ET(KwConstexpr); ET(KwConstCast); ET(KwContinue);
        ET(KwDefault); ET(KwDelete); ET(KwDo); ET(KwDouble); ET(KwDynamicCast);
        ET(KwElse); ET(KwEnum); ET(KwExplicit); ET(KwExport); ET(KwExtern);
        ET(KwFalse); ET(KwFloat); ET(KwFor); ET(KwFriend); ET(KwGoto);
        ET(KwIf); ET(KwInline); ET(KwInt); ET(KwLong); ET(KwMutable);
        ET(KwNamespace); ET(KwNew); ET(KwNoexcept); ET(KwNullptr);
        ET(KwOperator); ET(KwPrivate); ET(KwProtected); ET(KwPublic);
        ET(KwRegister); ET(KwReinterpretCast); ET(KwReturn); ET(KwShort);
        ET(KwSigned); ET(KwSizeof); ET(KwStatic); ET(KwStaticAssert);
        ET(KwStaticCast); ET(KwStruct); ET(KwSwitch); ET(KwTemplate);
        ET(KwThis); ET(KwThrow); ET(KwTrue); ET(KwTry); ET(KwTypedef);
        ET(KwTypeid); ET(KwTypename); ET(KwUnion); ET(KwUnsigned);
        ET(KwUsing); ET(KwVirtual); ET(KwVoid); ET(KwVolatile); ET(KwWhile);
    }
    return "<unknown>";
}

#undef ET

}  // namespace cppic