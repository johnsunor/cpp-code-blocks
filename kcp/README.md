### 概要

本部分源码主要为基于 [KCP](https://github.com/skywind3000/kcp) 协议所做的一些封装和测试，本文主要汇总了个人在 TCP、UDP、KCP 上的一些总结（环境以 Linux 和 IPv4 为主），在一些细节或有偏差的部分请以更正式的文档（RFC）、更具体的实现（Linux 协议栈）以及更实际的相关的实践或实验为主作参考。

从整体上来说 KCP 相当于在应用层实现了 TCP 的一些核心机制（可靠性、流量控制和拥塞控制），但并不负责底层数据的传输。在可靠性上面，KCP 主要实现了：1）正面确认，包含累积性确认和类似于 TCP 中 SACK 的选择性确认。2）在重传上包含有超时重传和快速重传 。4）重复分组去重以及乱序分组重排。实现中并不包含端到端的校验功能，这一点可以交给底层的通信层（如使用开启校验和的 UDP）或是由应用层自己来实现（如应用层可加入自己的 CRC 校验）。在流量控制上面，KCP 提供了类似于 TCP 中的滑动窗口机制来实现，发送缓冲区和接收缓冲区的大小初始时由用户自己设置。在拥塞控制上面（可以由用户选择性开启），KCP 也实现了拥塞窗口，借助快速重传可以激活快速恢复算法，在检测到超时丢包时，也可以触发慢启动。KCP 的优势主要在于在提供可靠性的条件下，可以降低（由用户控制）对于丢包的敏感度（退避），在超时重传上用户可以降低 RTO 的退避，通过开启 nodelay 机制可以加快 ack 的发送，降低延迟。在有流量控制的前提下，通过关闭拥塞控制，可以避免传送速度的突降。总体来说相对于 TCP 在一些有折中处理机制或规避处理的环节，KCP 可以以一种相对激进的方式来处理（更保守的退避），从而降低延迟，加快通信。当然其缺点也比较明显，带宽的利用率不够高（考虑 TCP 的 nagle 算法和 delayed-ack 机制）可能会进一步增加网络整体的拥塞。降低退避的处理，提升性能的同时，也损失了一定的公平性，侧面提升了自身通信的优先级和带宽占用率。

相对基于 [UDP](https://man7.org/linux/man-pages/man7/udp.7.html) 的通信来说，TCP 更适合需要进行大流量传输的通信，以及对综合稳定性要求比较高的通信。当前和 TCP 相关的很多设计和优化都需要通讯时流量满足一定条件后才能发挥比较好的效果，比如选择性重传 [SACK](https://en.wikipedia.org/wiki/Retransmission_(data_networks))，比如用于激活发送端 [快速重传](https://en.wikipedia.org/wiki/TCP_congestion_control#Fast_retransmit) 的三个 DACK， 如果中间的某个 TCP 分节丢失的时候，接收端只有在接收到更多的数据包的情况下才可以激活这些功能。再比如网卡 TSO 功能，通过网卡的配合来减少协议栈的分段压力，也是需要通信量足够的时候才会有效果。当通信量不是很大及网络链路又不是很稳定，噪音（delay, loss, duplicate, corrupt）比较多时，这些特性都很难发挥出来，实际中的通信效果可能就比较差。而基于 UDP 的通信，可以再应用层做更灵活的调整，比如可以做更细粒度的超时控制，更保守的 RTO 退避，携带更多的 SACK 块（TCP Options 由于空间限制最多只能携带四个 SACK 块），方便支持网络不稳定的客户端（比如移动端）做连接迁移，以及结合应用层的需要规避 [队头阻塞](https://en.wikipedia.org/wiki/Head-of-line_blocking)（由于TCP的发送缓冲区由内核控制，发送方在发送新 state 的数据时即使不想要缓冲区中已有的 old state 的数据时，也只能将新数据排在旧数据之后等待接收端的有序接收）相关的问题。

关于 TCP 的进一步总结文章在[这里](https://github.com/johnsunor/cpp-code-blocks/blob/master/kcp/TCP.md)。
****

### 测试
当前的封装基于 [muduo](https://github.com/chenshuo/muduo) ，通过 muduo 的 EventLoop 来进行事件的监听及分发，底层借助 [UDP](https://man7.org/linux/man-pages/man7/udp.7.html) 来进行通讯。封装之后我在公网上的两台服务器之间做了初步的测试，主要是可靠性测试和弱网络条件下的传输效率测试。测试环境为公网中两台主机，客户端主机 A 出口带宽为 1mbit，入口带宽 10mbit，服务端主机 B 出口带宽 5mbit，入口带宽为 10mbit，两主机间正常 RTT 为 30ms，根据需求可在主机 A 中通过 tc qdisc 添加信道噪音（延迟，重排序，重复，损坏，丢包等）模拟一条弱网络。测试部分代码位于 examples 目录下，弱网络模拟脚本位于 scripts 目录下。
1. `examples/diff` 功能性测试，即通过搭配 kcp 和 udp 测试两台主机是否可以在弱网络条件下实现消息的可靠传递。测试过程由主机 A 中的客户端随机生成长度范围在 [10, 14000] 内的 ASCII 字符串，然后再随机拆分为 10 部分，各部分按相对顺序将数据以间隔 10ms 的时间差分开发送到主机 B，主机 B 收到后将数据原样发送回来，主机 A 收到后汇总为一条消息，再和原始消息进行比对。经过多轮测试数据模拟，当前程序可以通过比对测试。
2. `examples/pingpong` 弱网络性能测试，在弱网络下通过和 muduo 自带的 [pingpong](https://github.com/chenshuo/muduo/tree/master/examples/pingpong) 吞吐量测试程序进行比较，通过发送相同大小的数据包进行传输效率的测试。测试中主要使用 4KB （对主机 A 中的客户端来说 BDP 大概为 4KB（1mbit / 8 * 30 / 1000) ）大小的数据块进行测试。信道中在没有添加噪音之前，进行测试发现程序中通过 KCP 的传输的效率大概为 TCP 传输效率的 94%，而添加噪音信号（丢包率在 5% ~ 10%）之后，通过多轮测试后发现通过 TCP （拥塞控制算法为 cubic）的传输效率要比 KCP 下降的多很多，KCP 的传输效率要比实验环境下的 TCP 要高 30% ~ 50%。通过对测试代码中应用层收到的数据量（约等于应用层发送的数据量）Bytes 和 tc qdisc 统计到的发送的总数据量 Bytes 进行比对，KCP 大概是 88%，TCP 大概是 90%。对于实验中模拟的条件来说，在应用层面相同的时间内基于 KCP 的传输确实可以起到更高的传输效率。有兴趣的读者可以尝试调整不同的 KCP 参数，模拟不同的网络环境，通过不同大小的数据包，以及打开/关闭网卡 TSO 等不同的环境下来观察一下 TCP 和 KCP 的表现。

### 实现
1）. 代码中主要包含 4 个核心的类，`class UDPSocket`，`class KCPSession`，`class KCPServer`，`class KCPClient`，基本描述如下：
 * `class UDPSocket` 用于封装 UDP socket 及相关 api 如：sendto, sendmsg, sendmmsg 等，为上层 KCP session 交互提供通讯能力，对象生命周期由 KCPServer/KCPClient 通过 std::unique_ptr 控制。
 * `class KCPSession` 用于封装类似于 TCP connection 的概念，一个 session 对象的成功创建即表示 client 和 server 之间新建立一条连接，KCPSession 对象生命期是模糊的，由 KCPServer/KCPClient 及用户共享，代码中通过 std::shared_ptr 管理，对于 KCP 控制块的封装也是在 KCPSession 中。
 * `class KCPServer` 持有 server 端 UDP socket，管理并创建 server 端 KCPSession，接收来自 client 的 UDP 数据包，并根据数据包类型做分发处理。KCPServer 由用户直接使用，生命期由用户控制。
 * `class KCPClient` 持有 client 端 UDP socket，管理 client 端 KCPSession 的创建，接收来自 server 的 UDP 数据包，并根据数据包类型做分发处理。KCPClient 由用户直接使用，生命期由用户控制。

2）. KCPSession 通过握手的形式进行连接的建立，每个连接通过唯一的 32bit session id 进行标识，通过 session id 标识连接的形式方便支持在客户端地址变化的时候做连接迁移（Connection Migration）。实现中将底层 UDP 数据包分为 6 类：

 * SYN_PACKET，SYN 分节，client 端通过 SYN 分节向 server 端发起 KCPSession 连接握手，server 端收到 SYN 分节后先对 client 端的地址做状态验证，满足条件后随机生成 session id 然后随 server 端的 SYN 分节发送到 client 端。
 * ACK_PACKET，ACK 分节，client 收到 server 端 SYN 分节后，向 server 端发送 ACK 分节，此时对于 client 来说连接已经成功建立，可以发送数据。
 * RST_PACKET，RST 分节，用于重置一个连接，client 和 server 端通信时如果检测到连接的状态异常时可以通过发送 RST 分节来结束一个连接。比如 server 收到 client 数据包时检测到对应的 session 不存在时就会发送该分节。另外，当前实现中 KCPSession 连接的正常断开也是通过做 KCP 状态刷新后跟着发送一个 RST 分节来实现的。
 * PING_PACKET，PING 分节，client 端会定时向 server 端发送 PING 分节来探测连接是否存活，server 端收到 PING 分节后会响应 client 端以 PONG 分节，如果长时间未收到 client 端的 PING 分节，server 端会关闭相应的 session。
 * PONG_PACKET，PONG 分节，用于 server 端响应 client 端的 PING 分节，如果 client 一段时间内没有收到 server 端数据（即没有收到 PONG 分节也没有收到 DATA 分节），cient 会重置本端的 session。
 * DATA_PACKET，DATA 分节，client 端和 server 端双向传递数据时都会使用的分节，上述中的其它分节都不经过 KCP 控制块的处理即可传输，即相应的数据包由 KCPServer 或 KCPClient 直接调用 UDP 接口发出，而 DATA 分节则是需要经过 KCP 控制块（封装在 KCPSession 中）的处理后（封装，分段）的在择机（根据流量控制和拥塞控制规则，以及定时器的调度）进行发送数据。

连接的建立（SYN - SYN - ACK）整体类似于 TCP 中的三路握手（SYN - SYNACK - ACK），正常的连接建立成功需要一个 RTT，如果某一路分节丢失 client 端或 server 端会定时重传，重传达到一定次数之后会放弃握手，连接建立失败。server 端收到 SYN 分节后在响应 client 端之前会记录一个 pending session（待完成握手的 session）类似于 TCP 中的半连接（TCP 中有半连接队列维护半连接，并有队列长度限制，可由内核参数 net.ipv4.tcp_max_syn_backlog 设置），握手完成后 client 端和 server 端会分别创建 KCPSession 对象，即此时双方连接建立成功，client 端的 session 会由KCPClient 维护并定时向 server 端发送 PING 分节保持活跃。server 端的 session 会通过 unordered_map 统一维护，并定时检测 session 的状态。对于 server 端来说并没有类似于 TCP 中的全连接队列（accept，net.core.somaxconn）的概念。

3). KCPSession 连接的断开比较特殊，这里并没有模仿 TCP 那样经过四路握手来断开连接，当前实现中弱化了连接主动断开的概念，可以减少设计的复杂度。KCPSession 中的 Close 接口时只是执行 slient close 并不会主动通知对端，对端可以通过 PING 或 RST 分节间接的检测到 session 的断开。对于 TCP 来说，连接的状态会由内核维护，即时用户层程序崩溃了，os 也会自动处理四路握手和对端协议栈通信，而基于 UDP 在用户层面的实现则不行。对于连接断开的检测实现中主要是通过 PING-PONG 交互来检测的。对于应用层来说，决定什么时候断开连接并可靠的通知到对端，比较好的方法是使用应用层级别的通知，比如接收端可以通过应用层级别的 ACK 来通知发送端数据已经接受完毕，可以执行后续的处理（比如关闭连接）。实现中 server 端会维护一个 time_wait_session_map_ 里面会将连接断开后的 session id 缓存一段时间，避免后续短时间内新建立的 session 和之前的 session 出现串话。

### 基本使用
```cpp
// 整体参考 Google C++ 编码风格
muduo::net::EventLoop loop;
muduo::net::InetAddress address(ip, port);

// client 端
KCPClient client(&loop);

// 连接建立或断开 callback
auto on_connection =
  [](const KCPSessionPtr& session, bool connected) {
    LOG_INFO << "session: " << session->session_id()
              << (connected ? " up" : " down");
    if (connected) {
      std::string message("Hello World!");
      session->Write(message.data(), message.size());
    }
  };

// 用户层消息到达 callback
auto on_message =
  [](const KCPSessionPtr& session, Buffer* buf) {
    LOG_INFO << "message from server: " << buf->retrieveAllAsString();
  };

// 设置回调函数
client.set_connection_callback(on_connection);
client.set_message_callback(on_message);

// 连接服务端
client.ConnectOrDie(address);

// 开启事件循环
loop.loop();

// server 端
KCPServer server(&loop);

auto on_connection =
  [](const KCPSessionPtr& session, bool connected) {
    LOG_INFO << "session: " << session->session_id()
              << (connected ? " up" : " down");
  };
auto on_message =
  [](const KCPSessionPtr& session, Buffer* buf) {
    LOG_INFO << "message from client: " << buf->as_string();
    session->Write(buf);
  };

// 设置回调函数
server.set_connection_callback(on_connection);
server.set_message_callback(on_message);

// 启动监听
server.ListenOrDie(address);

// 开启事件循环
loop.loop();
```

### 构建
本地需要先构建好 [muduo](https://github.com/chenshuo/muduo) 网络库，并且编译器需要支持 C++14，当本地条件都满足后，可以通过命令：MUDUO_BUILD_DIR=XXX(muduo 库所在的目录) bash ./build.sh 进行构建
