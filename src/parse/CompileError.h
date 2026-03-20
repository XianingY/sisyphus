#ifndef COMPILE_ERROR_H
#define COMPILE_ERROR_H

#include <stdexcept>
#include <string>

namespace sys {

class CompileError : public std::runtime_error {
public:
  explicit CompileError(const std::string &msg): std::runtime_error(msg) {}
};

}

#endif
