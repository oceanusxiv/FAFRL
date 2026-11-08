#include "ensure_sim_thread.hpp"

#include "moho/sim/SimThreadRole.h"
#include "platform/posix/FpEnvPosix.h"

namespace faf::py {
auto EnsureSimThread() -> void {
#if defined(_WIN32)
    platform::SetX87PrecisionControl(_PC_24);
#else
    platform::posix::SetSimFloatingPointEnvironment();
#endif
    moho::tIsSimThread = true;
}
} // namespace faf::py