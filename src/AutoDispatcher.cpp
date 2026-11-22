#include "AutoDispatcher.hpp"

namespace opera {

AutoDispatcher::AutoDispatcher(DispatchCoordinator& coordinator, int maxPerRun)
    : coord_(coordinator), maxPerRun_(maxPerRun > 0 ? maxPerRun : 1) {}

std::vector<DispatchResult> AutoDispatcher::run() {
    std::vector<DispatchResult> done;
    if (!enabled_) {
        return done;
    }
    for (int i = 0; i < maxPerRun_; ++i) {
        const DispatchResult r = coord_.dispatchNext();
        lastStop_ = r.status;
        if (r.status != DispatchStatus::Dispatched) {
            break;  // empty queue, or the most urgent case has to wait
        }
        done.push_back(r);
        ++total_;
    }
    return done;
}

}  // namespace opera
