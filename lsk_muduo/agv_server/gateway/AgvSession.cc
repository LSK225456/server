#include "AgvSession.h"

namespace agv {
namespace gateway {

AgvSession::AgvSession(const std::string& id)
    : agv_id_(id),
      last_active_time_(lsk_muduo::Timestamp::now()),
      battery_level_(100.0),
      state_(ONLINE),
      pose_{0.0, 0.0, 0.0, 1.0} {
    // 默认满电、在线、位于原点
}

}  // namespace gateway
}  // namespace agv
