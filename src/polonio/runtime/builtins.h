#pragma once

#include <vector>

#include "polonio/runtime/value.h"

namespace polonio {

class Env;
class Interpreter;
struct Location;

void install_builtins(Env& env);

}
