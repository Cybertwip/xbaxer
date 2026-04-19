// cleng_cc1_main_inc.cpp — pull cc1_main.cpp into our cgo bridge.
// driver.cpp dispatches `-cc1` invocations to cc1_main(), which lives only
// in the clang executable's own object set. We include it directly for the
// same reason as cleng_driver_inc.cpp.
#include "../../../llvm-project/clang/tools/driver/cc1_main.cpp"
