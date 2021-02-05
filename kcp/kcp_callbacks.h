#ifndef KCP_CALLBACKS_H
#define KCP_CALLBACKS_H

#include <sys/socket.h>

#include <functional>
#include <memory>

namespace muduo {
namespace net {

class Buffer;
class InetAddress;
}  // namespace net
}  // namespace muduo

class KCPSession;

using KCPSessionPtr = std::shared_ptr<KCPSession>;

using ConnectionCallback = std::function<void(const KCPSessionPtr&, bool)>;

using MessageCallback =
    std::function<void(const KCPSessionPtr&, muduo::net::Buffer*)>;

using WriteCompleteCallback = std::function<void(const KCPSessionPtr&)>;

using HighWaterMarkCallback = std::function<void(const KCPSessionPtr&, size_t)>;

using OutputCallback =
    std::function<void(void*, size_t, uint32_t, const muduo::net::InetAddress&)>;

using FlushTxQueueCallback = std::function<void()>;

using ErrorMessageCallback = std::function<void(struct cmsghdr& cmsg)>;

#endif
