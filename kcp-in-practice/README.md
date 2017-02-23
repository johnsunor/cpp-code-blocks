
本部分源码为基于[KCP](https://github.com/skywind3000/kcp)协议所做的一些实践

###关于[TCP](http://man7.org/linux/man-pages/man7/tcp.7.html)                                                                                                                                                                
在网络层(IP)的基础上为通信双方提供了可靠的、面向流的、全双工的、基于连接的通信方式。

####1. [TCP的可靠性](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Reliable_transmission)
TCP的可靠性传送在信息论与编码论中，属于是一种“尽力而为”的行为，即通过简单的尝试重复发送（ARQ）来进行尽力投递（delivery），在实现中主要通过下列方式来提供可靠性：
> * 积极确认（positive acknowledgement）
> * 丢失分组重传（timeout based retransmission & dupack based retransmission）
> * 重复分组检测（duplicate packets discard）
> * 乱序分组重排（reconstructed in order）
> * 错误检测（end-to-end checksum, weak check）

####2. [TCP的流量控制](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Flow_control)
TCP使用端到端的流量控制方式，主要用于避免数据在发送和接收上出现速度适配，行为上主要受滑动窗口来控制，滑动窗口的大小一般由接收方通告（offer），窗口大小的调控需考虑通信管道的容量（带宽与时延的乘积，即bandwith x rtt），不能过大（可能会增加数据传送的延迟），也不能过小（不利于吞吐量）。除此之外，发送端用于实现拥塞避免的拥塞窗口也控制着发送端数据发送行为，以及一般为了避免网络中小分组过多的nagle算法和为了利用捎带（piggy）机制的dalayed-ack算法也起到了一定的流量控制行为。
> * 滑动窗口式流量控制（[sliding window](https://en.wikipedia.org/wiki/Sliding_window_protocol)）
> * 用于拥塞避免的拥塞窗口（[congestion window](https://en.wikipedia.org/wiki/TCP_congestion_control#Congestion_window)）
> * 用于提高网络利用率的[tcp-nagle](https://en.wikipedia.org/wiki/Nagle's_algorithm)算法和[delayed-ack](https://en.wikipedia.org/wiki/TCP_delayed_acknowledgment)算法

####3. [TCP的拥塞控制](https://en.wikipedia.org/wiki/TCP_congestion_control)
TCP的拥塞控制主要用于控制数据进入网络的速率，避免网络拥堵，提高网络传输整体的性能。因为通信状况是动态变化的，所以下面所列出的各项机制往往是在通信过程中是动态的相互配合使用的。传统的TCP实现对于网络拥塞的感知是以丢包为信号的，当丢包发生时，会分析丢包的原因（超时或快速重传），然后触发不同的机制（慢启动或拥塞避免）来进行拥塞控制（收敛拥塞窗口和慢启动门限）。抑或借助网络设备（一般受限于网络设备的部署及更新换代）对于拥塞的感知（路由器的ECN能力）来在丢包出现前提前进行拥塞控制。不过随着网络通信的发展，基于丢包的拥塞控制算法已经有点过时了，有时并不能反馈真实的网络拥塞情况，对于丢包的过渡反馈以及典型的[bufferboat](https://en.wikipedia.org/wiki/Bufferbloat)问题会使吞吐量和延迟受到明显的影响。Google开源的[BBR](http://git.kernel.org/cgit/linux/kernel/git/davem/net-next.git/commit/?id=0f8782ea14974ce992618b55f0c041ef43ed0b78)(基于拥塞的拥塞的控制)算法，在实践中对于吞吐量的提高和延迟的降低都取得了不错的效果。
> * 慢启动[slow start](https://en.wikipedia.org/wiki/TCP_congestion_control#Slow_start)
> * 拥塞避免[congestion window](https://en.wikipedia.org/wiki/Congestion_window)
> * 用于激活快速恢复算法[fast recovery](http://www.isi.edu/nsnam/DIRECTED_RESEARCH/DR_WANIDA/DR/JavisInActionFastRecoveryFrame.html)的快速重传算法[fast retransmit](https://en.wikipedia.org/wiki/TCP_congestion_control#Fast_retransmit)
> * 需借助网络设备的显式拥塞通知机制[explicit congestion notification](https://en.wikipedia.org/wiki/Explicit_Congestion_Notification)

