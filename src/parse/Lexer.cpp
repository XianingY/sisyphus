#include "Lexer.h"
#include "CompileError.h"
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <map>
#include <sstream>

using namespace sys;

std::map<std::string, Token::Type> keywords = {
  { "if", Token::If },
  { "else", Token::Else },
  { "while", Token::While },
  { "for", Token::For },
  { "return", Token::Return },
  { "int", Token::Int },
  { "float", Token::Float },
  { "void", Token::Void },
  { "const", Token::Const },
  { "break", Token::Break },
  { "continue", Token::Continue },
};

Token Lexer::nextToken() {
  if (loc >= input.size())
    return Token::End;
  auto has = [&](int delta = 0) -> bool {
    return loc + delta < (int) input.size();
  };
  auto peekc = [&](int delta = 0) -> char {
    return input[loc + delta];
  };
  auto fail = [&](const std::string &msg) -> Token {
    std::ostringstream os;
    os << "lexer error at line " << lineno << ": " << msg;
    throw CompileError(os.str());
  };

  // Skip whitespace
  while (loc < input.size() && std::isspace((unsigned char) input[loc])) {
    if (input[loc] == '\n')
      lineno++;
    loc++;
  }

  // Hit end of input because of skipping whitespace
  if (loc >= input.size())
    return Token::End;

  char c = input[loc];

  // Identifiers and keywords
  if (std::isalpha((unsigned char) c) || c == '_') {
    std::string name;
    while (loc < input.size() &&
           (std::isalnum((unsigned char) input[loc]) || input[loc] == '_'))
      name += input[loc++];

    if (keywords.count(name))
      return keywords[name];

    // Pay special attention to stoptime() and starttime().
    // They are macros; we add in line number here.
    if (name == "stoptime")
      return Token("_sysy_stoptime_" + std::to_string(lineno));
    if (name == "starttime")
      return Token("_sysy_starttime_" + std::to_string(lineno));
    return Token(name);
  }

  // Integer/FP literals
  if (std::isdigit((unsigned char) c) || c == '.') {
    int start = loc;
    bool isFloat = false;
    bool sawExp = false;

    if (c == '0') {
      if (has(1) && (peekc(1) == 'x' || peekc(1) == 'X')) {
        // Hexadecimal, skip '0x'
        loc += 2;
        bool hasHexDigit = false;
        while (has() && (std::isxdigit((unsigned char) peekc()) || peekc() == '.')) {
          if (std::isxdigit((unsigned char) peekc()))
            hasHexDigit = true;
          if (peekc() == '.') {
            // Already seen a '.' before. Shouldn't continue.
            if (isFloat)
              break;
            
            isFloat = true;
          }
          loc++;
        }
        if (!hasHexDigit)
          return fail("invalid hexadecimal literal");

        // Try to read a 'p' for exponent.
        if (has() && (peekc() == 'p' || peekc() == 'P')) {
          isFloat = true;
          sawExp = true;
          loc++;

          if (has() && (peekc() == '+' || peekc() == '-'))
            loc++;

          int expStart = loc;
          while (has() && std::isdigit((unsigned char) peekc()))
            loc++;
          if (loc == expStart)
            return fail("hex float exponent has no digits");
        }
        if (isFloat && !sawExp)
          return fail("hex float literal missing exponent");

        std::string raw = input.substr(start, loc - start);
        if (isFloat) {
          char *end = nullptr;
          float value = strtof(raw.c_str(), &end);
          if (!end || *end != '\0')
            return fail("invalid float literal '" + raw + "'");
          return Token(value);
        }
        try {
          size_t pos = 0;
          int value = std::stoi(raw, &pos, /*base = autodetect*/0);
          if (pos != raw.size())
            return fail("invalid integer literal '" + raw + "'");
          return Token(value);
        } catch (...) {
          return fail("invalid integer literal '" + raw + "'");
        }
      }

      // Octal. But let `std::stoi` to check for it.
      // Fall through here.
    }

    // Now this is a normal decimal integer or FP.
    while (has() && (std::isdigit((unsigned char) peekc()) || peekc() == '.')) {
      if (peekc() == '.') {
        // Already seen a '.' before. Shouldn't continue.
        if (isFloat)
          break;
        
        isFloat = true;
      }
      loc++;
    }

    // Try to read an 'e' for exponent.
    if (has() && (peekc() == 'e' || peekc() == 'E')) {
      isFloat = true;
      sawExp = true;
      loc++;

      if (has() && (peekc() == '+' || peekc() == '-'))
        loc++;

      int expStart = loc;
      while (has() && std::isdigit((unsigned char) peekc()))
        loc++;
      if (loc == expStart)
        return fail("float exponent has no digits");
    }

    std::string raw = input.substr(start, loc - start);
    if (isFloat) {
      char *end = nullptr;
      float value = strtof(raw.c_str(), &end);
      if (!end || *end != '\0')
        return fail("invalid float literal '" + raw + "'");
      return Token(value);
    }
    try {
      size_t pos = 0;
      int value = std::stoi(raw, &pos, /*base = autodetect*/0);
      if (pos != raw.size())
        return fail("invalid integer literal '" + raw + "'");
      return Token(value);
    } catch (...) {
      return fail("invalid integer literal '" + raw + "'");
    }
  }

  // Check for multi-character operators like >=, <=, ==, !=, +=, etc.
  if (loc + 1 < input.size()) {
    switch (c) {
    case '=': 
      if (input[loc + 1] == '=') { loc += 2; return Token::Eq; }
      break;
    case '>':
      if (input[loc + 1] == '=') { loc += 2; return Token::Ge; }
      break;
    case '<': 
      if (input[loc + 1] == '=') { loc += 2; return Token::Le; }
      break;
    case '!': 
      if (input[loc + 1] == '=') { loc += 2; return Token::Ne; }
      break;
    case '+': 
      if (input[loc + 1] == '=') { loc += 2; return Token::PlusEq; }
      break;
    case '-': 
      if (input[loc + 1] == '=') { loc += 2; return Token::MinusEq; }
      break;
    case '*': 
      if (input[loc + 1] == '=') { loc += 2; return Token::MulEq; }
      break;
    case '/': 
      if (input[loc + 1] == '=') { loc += 2; return Token::DivEq; }
      if (input[loc + 1] == '/') { 
        // Loop till we find a line break, then retries to find the next Token
        // (we can't continue working in the same function frame)
        for (; loc < input.size(); loc++) {
          if (input[loc] == '\n')
            return nextToken();
        }
        return Token::End;
      }
      if (input[loc + 1] == '*') {
        // Skip '/*', and loop till we find '*/'.
        loc += 2;
        for (; loc < input.size(); loc++) {
          if (input[loc] == '\n')
            lineno++;
          if (loc + 1 < input.size() && input[loc] == '*' && input[loc + 1] == '/') {
            // Skip '*/'.
            loc += 2;
            return nextToken();
          }
        }
        return fail("unterminated block comment");
      }
      break;
    case '%': 
      if (input[loc + 1] == '=') { loc += 2; return Token::ModEq; }
      break;
    case '&': 
      if (input[loc + 1] == '&') { loc += 2; return Token::And; }
      break;
    case '|': 
      if (input[loc + 1] == '|') { loc += 2; return Token::Or; }
      break;
    default:
      break;
    }
  }

  // Single-character operators and symbols
  switch (c) {
  case '+': loc++; return Token::Plus;
  case '-': loc++; return Token::Minus;
  case '*': loc++; return Token::Mul;
  case '/': loc++; return Token::Div;
  case '%': loc++; return Token::Mod;
  case ';': loc++; return Token::Semicolon;
  case '=': loc++; return Token::Assign;
  case '!': loc++; return Token::Not;
  case '(': loc++; return Token::LPar;
  case ')': loc++; return Token::RPar;
  case '[': loc++; return Token::LBrak;
  case ']': loc++; return Token::RBrak;
  case '<': loc++; return Token::Lt;
  case '>': loc++; return Token::Gt;
  case ',': loc++; return Token::Comma;
  case '{': loc++; return Token::LBrace;
  case '}': loc++; return Token::RBrace;
  default:
    return fail(std::string("unexpected character '") + c + "'");
  }
}

bool Lexer::hasMore() const {
  return loc < input.size();
}
