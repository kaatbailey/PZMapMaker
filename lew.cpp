#include "lew.hpp"

// LEW is header-only: every method is a few instructions on the hot path of
// every write, and none of them need a translation unit. This file exists so
// the target has a stable place to put anything that later does.
