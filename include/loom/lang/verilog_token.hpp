#pragma once

#include <loom/lang/token.hpp>
#include <string>
#include <unordered_map>

namespace loom {

enum class VerilogTokenType {
    // Literals
    Identifier,
    EscapedIdentifier,  // \escaped_id
    Number,             // decimal, hex, binary, octal
    StringLiteral,

    // Keywords (subset — full list in keyword table)
    KwModule,
    KwEndmodule,
    KwInput,
    KwOutput,
    KwInout,
    KwWire,
    KwReg,
    KwParameter,
    KwLocalparam,
    KwAssign,
    KwAlways,
    KwInitial,
    KwBegin,
    KwEnd,
    KwIf,
    KwElse,
    KwCase,
    KwCasex,
    KwCasez,
    KwEndcase,
    KwFor,
    KwWhile,
    KwGenerate,
    KwEndgenerate,
    KwFunction,
    KwEndfunction,
    KwTask,
    KwEndtask,
    KwDefparam,
    KwDefault,
    KwPosedge,
    KwNegedge,
    KwOr,
    KwAnd,
    KwNot,
    KwSupply0,
    KwSupply1,
    KwInteger,
    KwReal,
    KwTime,
    KwGenvar,

    // SV keywords (extensions)
    KwLogic,
    KwBit,
    KwByte,
    KwShortint,
    KwInt,
    KwLongint,
    KwInterface,
    KwEndinterface,
    KwPackage,
    KwEndpackage,
    KwClass,
    KwEndclass,
    KwImport,
    KwExport,
    KwTypedef,
    KwEnum,
    KwStruct,
    KwUnion,
    KwVirtual,
    KwExtends,
    KwImplements,
    KwModport,
    KwClocking,
    KwEndclocking,
    KwProperty,
    KwEndproperty,
    KwSequence,
    KwEndsequence,
    KwAssert,
    KwAssume,
    KwCover,
    KwConstraint,
    KwRand,
    KwRandc,
    KwUnique,
    KwPriority,
    KwAlwaysComb,
    KwAlwaysFf,
    KwAlwaysLatch,
    KwForeach,
    KwReturn,
    KwVoid,
    KwAutomatic,
    KwStatic,
    KwConst,
    KwRef,
    KwProgram,
    KwEndprogram,

    // Preprocessor directives
    Directive,  // `define, `include, `ifdef, `endif, etc.

    // Operators and punctuation
    LParen,     // (
    RParen,     // )
    LBracket,   // [
    RBracket,   // ]
    LBrace,     // {
    RBrace,     // }
    Semicolon,  // ;
    Colon,      // :
    Comma,      // ,
    Dot,        // .
    Hash,       // #
    At,         // @
    Assign,     // =
    NonBlocking,// <=  (also comparison, context-dependent)
    Plus,       // +
    Minus,      // -
    Star,       // *
    Slash,      // /
    Percent,    // %
    Ampersand,  // &
    Pipe,       // |
    Caret,      // ^
    Tilde,      // ~
    Bang,       // !
    Question,   // ?
    DoubleColon,// ::
    Arrow,      // ->
    FatArrow,   // =>
    DoubleEq,   // ==
    NotEq,      // !=
    TripleEq,   // ===
    TripleNotEq,// !==
    LessEq,     // <= (same lexeme as NonBlocking — disambiguated by parser)
    GreaterEq,  // >=
    Less,       // <
    Greater,    // >
    LShift,     // <<
    RShift,     // >>
    LogAnd,     // &&
    LogOr,      // ||
    Power,      // **

    // Special
    Eof,
    Unknown
};

using VerilogToken = Token<VerilogTokenType>;

// Keyword lookup: string -> VerilogTokenType (or nullopt)
const std::unordered_map<std::string, VerilogTokenType>& verilog_keywords();

// Token type name for debugging
const char* verilog_token_name(VerilogTokenType t);

} // namespace loom
