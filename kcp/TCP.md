
### 关于 [TCP](http://man7.org/linux/man-pages/man7/tcp.7.html)                                                                                                                                                                
TCP 是在网络层（IP）的基础上为通信双方提供了可靠的、面向流的、全双工的、基于连接的通信方式，其核心有三点：可靠性、流量控制和拥塞控制。

#### 1. [TCP 的可靠性](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Reliable_transmission)
TCP 的可靠性传送在信息论与编码论中属于是一种 “尽力而为” 的行为，在发送端检测到丢包（或因为差错被对端丢弃）时通过简单的尝试 “重复发送” 即 ARQ（ARQ 也可以被称之为 “Backward error correction”，关于差错控制另一种典型的处理方式是 FEC（[Forward error correction](https://en.wikipedia.org/wiki/Forward_error_correction))） 来进行尽力投递（delivery），但不保证数据一定会被递送到，也不保证被递送到的数据一定准确无误（TCP 校验和是一种弱校验）。对于递送成功（Linux 下不是指 write 系统调用成功返回）是指接收到了对端协议栈的确认（不代表对端应用成功读取以及应用级确认），对于递送失败（未成功收到对端协议栈确认）它可以给上层提供可靠的错误反馈（errno、SO_ERROR、poll-event 等）。在实现中 TCP 主要通过下列方式来提供可靠性：
> * 正面确认（positive acknowledgement）
> * 丢失分组重传（timeout based retransmission & dupack based retransmission）
> * 重复分组检测（duplicate packets discard）
> * 乱序分组重排（reconstructed in order）
> * 错误检测（end-to-end checksum, weak check）

#### 2. [TCP的流量控制](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Flow_control)
TCP 使用端到端的流量控制方式，主要用于避免数据在发送和接收上出现速度失配，行为上主要受滑动窗口来控制，滑动窗口的大小一般由接收方通告（offer），窗口大小的调控需考虑通信管道的容量（带宽与时延的乘积，即 Bandwidth X RTT），不能过大（可能会增加数据传送的延迟），也不能过小（不利于吞吐量）。除此之外，发送端用于实现拥塞避免的拥塞窗口也控制着发送端数据发送行为，以及一般为了避免网络中小分组过多的 nagle 算法和为了利用捎带（piggy）机制的 dalayed-ack 算法也起到了一定的流量控制行为。
> * 滑动窗口式流量控制（[sliding window](https://en.wikipedia.org/wiki/Sliding_window_protocol)）
> * 用于拥塞避免的拥塞窗口（[congestion window](https://en.wikipedia.org/wiki/TCP_congestion_control#Congestion_window)）
> * 用于提高网络利用率的 [tcp-nagle](https://en.wikipedia.org/wiki/Nagle's_algorithm) 算法和 [delayed-ack](https://en.wikipedia.org/wiki/TCP_delayed_acknowledgment) 算法

#### 3. [TCP的拥塞控制](https://en.wikipedia.org/wiki/TCP_congestion_control)
TCP的拥塞控制主要用于控制数据进入网络的速率，避免网络拥堵，提高网络传输整体的性能。因为通信状况是动态变化的，所以下面所列出的各项机制往往是在通信过程中是动态的相互配合使用的。传统的TCP实现对于网络拥塞的感知是以丢包为信号的，当丢包发生时，会分析丢包的原因（超时或快速重传，更一般的来说，关于数据包在网络中传输被丢弃的原因典型的有三种：1）网络中中间设备如路由器缓冲区队列满了或检测到近期队列长度超过某个阈值（拥塞）而直接丢包。2）数据包损坏，校验和检测不合法（如链路层CRC校验或接收端TCP校验和出错等）而被抛弃。3）被某些有特殊作用的网络中间件（比如流量过滤、拦截设备等）抛弃掉。），然后触发不同的机制（慢启动或拥塞避免）来进行拥塞控制（收敛拥塞窗口和慢启动门限）。抑或借助网络设备（一般受限于网络设备的部署及更新换代）对于拥塞的感知（如路由器的ECN能力）来在丢包出现前提前进行拥塞控制。不过随着网络通信的发展，基于丢包的拥塞控制算法已经有点过时了，有时并不能反馈真实的网络拥塞情况，对于丢包的过度反馈以及典型的[bufferboat](https://en.wikipedia.org/wiki/Bufferbloat)问题会使吞吐量和延迟受到明显的影响。Google开源的[BBR](https://queue.acm.org/detail.cfm?id=3022184)(基于拥塞的拥塞的控制)算法，在实践中对于吞吐量的提高和延迟的降低都取得了不错的效果。
> * 慢启动[slow start](https://en.wikipedia.org/wiki/TCP_congestion_control#Slow_start)
> * 拥塞避免[congestion window](https://en.wikipedia.org/wiki/Congestion_window)
> * 用于激活快速恢复算法[fast recovery](http://www.isi.edu/nsnam/DIRECTED_RESEARCH/DR_WANIDA/DR/JavisInActionFastRecoveryFrame.html)的快速重传算法[fast retransmit](https://en.wikipedia.org/wiki/TCP_congestion_control#Fast_retransmit)
> * 需借助网络设备的显式拥塞通知机制[explicit congestion notification](https://en.wikipedia.org/wiki/Explicit_Congestion_Notification)

#### 4. 一些细节
##### [TCP](https://en.wikipedia.org/wiki/Transmission_Control_Protocol) Header Format
<table class="table table-bordered table-striped table-condensed" style="margin: 0 auto; text-align: center;">
<tr>
<th colspan="8"><center>0</center></th>
<th colspan="8"><center>1</center></th>
<th colspan="8"><center>2</center></th>
<th colspan="8"><center>3</center></th>
</tr>
<tr>
<th style="width:2.6%;">0</th>
<th style="width:2.6%;">1</th>
<th style="width:2.6%;">2</th>
<th style="width:2.6%;">3</th>
<th style="width:2.6%;">4</th>
<th style="width:2.6%;">5</th>
<th style="width:2.6%;">6</th>
<th style="width:2.6%;">7</th>
<th style="width:2.6%;">8</th>
<th style="width:2.6%;">9</th>
<th style="width:2.6%;">10</th>
<th style="width:2.6%;">11</th>
<th style="width:2.6%;">12</th>
<th style="width:2.6%;">13</th>
<th style="width:2.6%;">14</th>
<th style="width:2.6%;">15</th>
<th style="width:2.6%;">16</th>
<th style="width:2.6%;">17</th>
<th style="width:2.6%;">18</th>
<th style="width:2.6%;">19</th>
<th style="width:2.6%;">20</th>
<th style="width:2.6%;">21</th>
<th style="width:2.6%;">22</th>
<th style="width:2.6%;">23</th>
<th style="width:2.6%;">24</th>
<th style="width:2.6%;">25</th>
<th style="width:2.6%;">26</th>
<th style="width:2.6%;">27</th>
<th style="width:2.6%;">28</th>
<th style="width:2.6%;">29</th>
<th style="width:2.6%;">30</th>
<th style="width:2.6%;">31</th>
</tr>

<tr>
<td colspan="16" align="center">源端口</td>
<td colspan="16" align="center">目的端口</td>
</tr>

<tr>
<td colspan="32" align="center">序列号</td>
</tr>

<tr>
<td colspan="32" align="center">确认号</td>
</tr>

<tr>
<td colspan="4" align="center">头部长度</td>
<td colspan="3" align="center">保留位<br />
<tt><b>0 0 0</b></tt></td>

<td colspan="1" align="center"><a href="https://tools.ietf.org/html/rfc3540"><tt>N</tt><br />
<tt>S</tt></a></td>
<td colspan="1" align="center"><a href="https://tools.ietf.org/html/rfc3540"><tt>C</tt><br />
<tt>W</tt><br />
<tt>R</tt></a></td>
<td colspan="1" align="center"><a href="https://tools.ietf.org/html/rfc3540"><tt>E</tt><br />
<tt>C</tt><br />
<tt>E</tt></a></td>
<td colspan="1" align="center"><tt>U</tt><br />
<tt>R</tt><br />
<tt>G</tt></td>
<td colspan="1" align="center"><tt>A</tt><br />
<tt>C</tt><br />
<tt>K</tt></td>
<td colspan="1" align="center"><tt>P</tt><br />
<tt>S</tt><br />
<tt>H</tt></td>
<td colspan="1" align="center"><tt>R</tt><br />
<tt>S</tt><br />
<tt>T</tt></td>
<td colspan="1" align="center"><tt>S</tt><br />
<tt>Y</tt><br />
<tt>N</tt></td>
<td colspan="1" align="center"><tt>F</tt><br />
<tt>I</tt><br />
<tt>N</tt></td>

<td colspan="16" align="center">窗口大小</td>
</tr>

<tr>
<td colspan="16" align="center">校验和</td>
<td colspan="16" align="center">紧急指针</td>
</tr>

<tr>
<td colspan="32" style="background:#ffd0d0;" align="center">TCP选项(最多40字节)<br />
...</td>
</tr>
</table>

##### [IPv4](https://en.wikipedia.org/wiki/IPv4) Header Format
<table class="table table-bordered table-striped table-condensed" style="margin: 0 auto;text-align: center;">
<tr>
<th colspan="8"><center>0</center></th>
<th colspan="8"><center>1</center></th>
<th colspan="8"><center>2</center></th>
<th colspan="8"><center>3</center></th>
</tr>
<tr>
<th style="width:2.6%;">0</th>
<th style="width:2.6%;">1</th>
<th style="width:2.6%;">2</th>
<th style="width:2.6%;">3</th>
<th style="width:2.6%;">4</th>
<th style="width:2.6%;">5</th>
<th style="width:2.6%;">6</th>
<th style="width:2.6%;">7</th>
<th style="width:2.6%;">8</th>
<th style="width:2.6%;">9</th>
<th style="width:2.6%;">10</th>
<th style="width:2.6%;">11</th>
<th style="width:2.6%;">12</th>
<th style="width:2.6%;">13</th>
<th style="width:2.6%;">14</th>
<th style="width:2.6%;">15</th>
<th style="width:2.6%;">16</th>
<th style="width:2.6%;">17</th>
<th style="width:2.6%;">18</th>
<th style="width:2.6%;">19</th>
<th style="width:2.6%;">20</th>
<th style="width:2.6%;">21</th>
<th style="width:2.6%;">22</th>
<th style="width:2.6%;">23</th>
<th style="width:2.6%;">24</th>
<th style="width:2.6%;">25</th>
<th style="width:2.6%;">26</th>
<th style="width:2.6%;">27</th>
<th style="width:2.6%;">28</th>
<th style="width:2.6%;">29</th>
<th style="width:2.6%;">30</th>
<th style="width:2.6%;">31</th>
</tr>
<tr>
<td colspan="4" align="center">版本号</td>
<td colspan="4" align="center">首部长度</td>
<td colspan="6" align="center">区分服务码点</td>
<td colspan="2" align="center"><a href="https://tools.ietf.org/html/rfc3168#section-5">ECN</a></td>
<td colspan="16" align="center">IP包整体长度</td>
</tr>

<tr>
<td colspan="16" align="center">片标示</td>
<td colspan="3" align="center">分片标志</td>
<td colspan="13" align="center">13位片偏移</td>
</tr>

<tr>
<td colspan="8" align="center">生存期</td>
<td colspan="8" align="center">传输层协议号</td>
<td colspan="16" align="center">首部校验和</td>
</tr>

<tr>
<td colspan="32" align="center">源IP地址</td>
</tr>
<tr>
<td colspan="32" align="center">目的IP地址</td>
</tr>
<tr>
<td colspan="32" rowspan="4" align="center">IP选项（最多40字节）<br />...</td>
</tr>
</table>

##### TCP pseudo-header for checksum computation (IPv4)
<table class="table table-bordered table-striped table-condensed" style="margin: 0 auto;text-align: center;">
<tr>
<th colspan="4" style="width:11%;" align="center">0–3</th>
<th colspan="4" style="width:11%;" align="center">4–7</th>
<th colspan="8" style="width:22%;" align="center">8–15</th>
<th colspan="16" style="width:44%;" align="center">16–31</th>
</tr>
<tr>

<td colspan="32" style="background:#fdd;" align="center">源地址</td>
</tr>
<tr>
<td colspan="32" style="background:#fdd;" align="center">目的地址</td>
</tr>

<tr>
<td colspan="8" style="background:#fdd;" align="center">零</td>
<td colspan="8" style="background:#fdd;" align="center">协议类型（6）</td>
<td colspan="16" style="background:#fdd;" align="center">TCP分节长度</td>
</tr>

</table>

##### 1）[三次握手](http://www.tcpipguide.com/free/t_TCPConnectionEstablishmentProcessTheThreeWayHandsh-3.htm)
TCP 连接建立所需要的三次握手，是出于在不完全可靠的信道中进行通信（[Two_Generals'_Problem](https://en.wikipedia.org/wiki/Two_Generals%27_Problem)）时通信双方就某一问题达成一致所需要的最少交互要求。不过在实际通信时也有可能出现四次握手的情形（连接同时打开（[simultaneous open](http://www.tcpipguide.com/free/t_TCPConnectionEstablishmentProcessTheThreeWayHandsh-4.htm)））。同时在连接建立的过程中双方也会相互通告各自的ISN 、Sindow Size（用于对端建立滑动窗口），TCP Options 等，并完成各自通信所需资源的分配。

##### 2）[四次挥手](http://www.tcpipguide.com/free/t_TCPConnectionTermination-2.htm)
TCP连接断开时需要的四次挥手，相比连接建立时多出来的一次挥手，是因为 [half-close](http://www.vorlesungen.uni-osnabrueck.de/informatik/networking-programming/notes/22Nov96/3.html) 的存在，不过在通信中（如 Linux 下）也有可能会出现三次挥手就完成连接的断开（当被动关闭端没有后续数据要发送时可能将 ACK 和 FIN 分节（即第二路和第三路）进行[合并](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Connection_termination)）。

##### 3）序列号
32 位的序列号用于标示 TCP 发送端到接收端的数据流中的一个字节（在带有数据的分节中是数据部分的首个字节），SYN 分节中包含着发送端的 ISN 、FIN 分节包含着发送端的 LSN 。在 TCP 中凡是消耗 SN（使 SN 有效递增）的分节（SYN 、FIN 和带有 payload 的分节）都需要 “[重传](https://en.wikipedia.org/wiki/Retransmission_(data_networks))” 来提供可靠性保障。ISN 的生成需要考虑一定的[安全](https://tools.ietf.org/html/rfc1948)因素（被攻击者 predictable 到之后，可以向连接中注入伪造的数据包进行攻击），另外一个需要考虑的因素是避免快速回绕（wrap-around），在 [Linux2.6.39](http://lxr.free-electrons.com/source/drivers/char/random.c?v=2.6.39#L1528) 中典型的实现是通过结合通信双方的 IP 、Port 构成的四元组进行散列计算以及一个动态变化的时钟（每 64ns 加 1，32 位无符号数环绕一次周期约为 274 秒，这个周期也避免了网络中处于 MSL（[RFC793](https://tools.ietf.org/html/rfc793#section-3.3)建议值为 2min ， Linux 下典型值为 30s）内旧的无效的分组和 TCP 新产生的分组间的干扰）进行生成的。关于序列号的防回绕，另一个可以用的机制是 TCP 的[时间戳选项](https://tools.ietf.org/html/rfc7323)，后文会有更详细的介绍。

##### 4）TCP Flags
一些老的 TCP 实现中只能理解其中的后 6 位 flags，前三个主要和拥塞控制有关。

* [NS](https://tools.ietf.org/html/rfc3540#page-2)，随机和（Nonce Sum），这是一个实验性的 flag，主要目的是给 TCP 通信中的发送端（往往是服务端）一种机制来检查接收端的一些异常行为（恶意的（如接收端 TCP 进行[乐观 ACK 攻击](http://www.kb.cert.org/vuls/id/102014)或故意移除 ECE 标志）或非恶意的（如通信链路中的 NAT 设备，Router，Firewall 等可能由于不支持等原因将 ECE 标志清除）），异常的接收端可能会占用更多的带宽，破坏掉网络通信中带宽占用的公平性，提高自身的优先级。发送端会通过设置 IP 首部中的 ECN 字段随机一种 ECT 码点（codepoint，即 10-ECT(0) 和 01-ECT(1)），在没有被 CE（Congestion Experienced） 码点覆盖的情况下，接收端通过不断的累加 ECN 中对应 1 比特的数值从而计算出随机和（Nonce Sum），然后在 ACK 中将其填充至 TCP 首部中的 NS 字段传输到发送端，发送端会进行 NS 数值的校验。如果路由器设置 CE 码点，那么会覆盖掉 ECT 中的原始数值，这样接收端只能在一定的概率下（50%）计算出正确的 NS 数值，如果接收端正常处理了 CE，并设置 ECE 通知发送端，那么发送端在设置 CWR 时会暂停本次 NS 校验。当 ECE 正常响应之后，发送端会在后续的新的数据包中重新与接收端同步 NS 数值的计算，并进行后续的 NS 校验。如果，接收端出现异常（如前所述）清除掉中间路由器的 CE 通知（拥塞通告），那么发送端通过持续的 NS 校验便可以检测出接收端的异常行为（当拥塞频繁发生时，会越容易检测出接收端的异常行为，因为接收端对于 NS 数值计算正确的累计概率会一直下降）。
* [CWR](https://tools.ietf.org/html/rfc3540)，发送端拥塞窗口减小（Congestion Window Reduced），一般是当发送端收到接收端含有 [ECE](https://tools.ietf.org/html/rfc3540) 标志的 ACK 之后，会进行 CWND 的收敛（因为当接收端收到 CE 码点之后，会给后续的每个 ACK 都设置 ECE 标志，直到收到发送端的 CWR 通知，这里 CWND 的收敛不能收敛过渡，比如可以在每个 RTT 内收到的含有 ECE 的 ACK 中累计只收敛一次，这一点类似于处于“拥塞规避”机制（congestion avoidance）中的 CWND 增长时变化的特点），而且会在下一个发往接收端的最新的数据包中设置该选项（[When the TCP data sender is ready to set the CWR bit after reducing the congestion window, it SHOULD set the CWR bit only on the first new data packet that it transmits](https://tools.ietf.org/html/rfc3168#page-19)），以此来响应接收端的 ECE（如果含有 CWR 的响应包丢失了，发送端可能会再次收敛 CWND（比如检测到超时丢包），CWR 会在下一个新的数据包中设置，而重传的包中并不会设置 CWR）。该选项需配合 ECE 标志以及 IP 首部中的 ECN 字段协调使用，同时也需要通信两端以及通信链路中的设备支持才能起到特定的作用。另外，关于拥塞窗口的减小，典型有三种原因：
    * 1）超时重传（触发慢启动）。
    * 2）快速重传（触发快速恢复）。
    * 3）响应 ECE 并设置 CWR 。
* [ECE](https://tools.ietf.org/html/rfc3540)，[ECN](https://tools.ietf.org/html/rfc3168) 回显（ECN Echo），当通信中的接收端收到链路对于拥塞的感知（即 IP Header中 CE 码点（ECN bits为11）被设置，如当路由器感知到持续的拥塞（[Persistent Congestion](https://tools.ietf.org/html/rfc3168#page-9)）后设置（[AQM](https://en.wikipedia.org/wiki/Active_queue_management)，参考[RFC2309](https://tools.ietf.org/html/rfc2309)、[RFC3168](https://tools.ietf.org/html/rfc3168)）），会将后续的 TCP 分节中通过设置ECE标志来通知发送端提前进行拥塞（incipient congestion）控制，发送端收到 ECE 标志后会继而收敛 CWN 的大小，并设置 CWR 标志以响应接收端。这些协作机制可以避免网络中不必要的拥堵、丢包以及延迟，从而提升网络的性能。
* RST，重置（Reset），是在 TCP 发生错误时发送的一种分节，有三种情况下会产生 RST 分节：
    * 1）在未运行服务的某地址上收到 SYN 分节后，会向对端发送 RST 分节，对端在接收到 RST 分节后会返回 ECONNREFUSED 错误（hard error）。
    * 2）通信的某一端想取消一个连接（如通过设置 SO_LINGER 套接字选项后调用 close），如果服务端在进行 accept 时收到 RST 分节（客户端取消连接），那么服务端会返回 ECONNABORTED（或 EPROTO）错误（soft error），服务端需忽略这种错误。在连接已建立（至少是通信的某一端认为连接处于 ESTABLISHED 状态）的情况下收到 RST 分节后，会返回 ECONNRESET 错误，此时对应的连接应该被销毁（对于 EPOLL 此时往往会触发 IN、HUP、ERR 等事件，可通过 read 返回 0 来将 fd 关掉）。
    * 3）通信的某一端收到一个不存在（至少是收到数据的这一端认为连接不存在，如该端点主机崩溃并重启（即并未走正常的 TCP 连接断开时的四路挥手过程））的连接上的数据时会向另一端发送 RST 分节，此时连接也应该被销毁。另外，对于已经收到 RST 的 TCP 套接字再次进行写操作的话（如进程在连续的两次 write 操作之间收到 RST），进程将会收到 SIGPIPE 信号（errno 会被置为 EPIPE），对于该信号的默认动作是终止相应的进程，为了避免未期望的终止，一般可以忽略（SIG_IGN）该信号。另外，RST分节还可以起到的一种作用就是提前终止（"assassinated "）处于TIME_WAIT状态的套接字（["TIME-WAIT Assassination" (TWA)](https://tools.ietf.org/html/rfc1337#page-1)）。

##### 4）[TCP校验和](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_checksum_for_IPv4)（以IPV4为例）
TCP 的校验和是一种端到端的校验和，计算方式为对数据以 16 bits 为单位进行反码和的反码计算，数据的具体内容包含 12 字节伪头部、最多 60 字节的首部、理论上最多 65515 字节的应用层数据，并在需要时用 0 字节将总长度填充为偶数长）。TCP 的校验和是一种弱校验（weak check），数据在传输过程中如果出现双字节比特位反转，那么通过校验和将不能检测出这种错误，加上链路层（link layer）的校验和的一些缺点（在跨网段通信时路由器可能由于硬件错误而破坏数据）。所以，对于重要的数据在通信时一般也需要应用层提供更进一步的错误校验。

##### 5）[TCP选项](https://tools.ietf.org/html/rfc793#page-17)
位于 TCP Header 尾部的 TCP 选项提供了一种扩展 TCP 协议的能力，典型的分两种：1）单字节形式（如 EOL 、NOP 等）2）| kind | length | value |形式（如 MSS、WS 等）。其中 NOP 选项可用于在多个选项之间进行填充（padding）以使各个选项的起始位置按四字节对齐（不过 [RFC793](https://tools.ietf.org/html/rfc793#page-18) 中也提到了不能保证实现中一定这样做）。

* [MSS](https://en.wikipedia.org/wiki/Maximum_segment_size)，最大分段长度（Maximum Segment Size），通信的一端在连接建立时可以通知另一端在单个TCP分节中所能承载的数据量的最大大小（不包含 TCP Header 中固定大小部分）。设置该TCP选项的作用一般是：
   * 1）通知接收端实际可用的 MSS 大小（默认情况下发送端可用的 MSS 大小为 536 字节（IPv4），因为 IPv4 中对于分片所需的最小重组缓冲区大小为 576 字节，去除 IP 首部和 TCP 首部中各自固定大小的 20 字节，留给 MSS 的大小为 536 字节（实际使用中一般按 512 字节使用））。
   * 2）避免 IP 层进行分片（fragmentation），对于 IP 层来说，需要考虑链路层 MTU 的大小，结合 MTU 进行分片，当所有分片到达对端之后再进行重组（reassembling），如果在传输的过程中任何一片丢失（接收端 IP 层计时器超时）的话，那么 IP 将会丢弃掉所有之前收到的分片，那么对于上层提供可靠传输的协来说将不得不重传整个 IP 数据包（分片前的数据包），这样就加大了重传的开销（overhead），所以这一点对于 TCP 来说比较重要。该选项在 TCP Header 的 Options 中本身占用 4Bytes，其中 value 部分占用 2Bytes（| kind = 2 | length = 4 | value (以太网中典型值为 1460 ) |）。另外，该选项是在随着 TCP 连接的建立即 SYN 分节发送到对端的，在 TCP 连接建立时该数值一般由操作系统根据[协议栈的反馈](https://en.wikipedia.org/wiki/Maximum_segment_size#Inter-Layer_Communication)自己来设定 。
* [WS](https://en.wikipedia.org/wiki/TCP_window_scale_option)，窗口规模选项（Window Scale Option），用于扩大接收缓冲区的大小，默认情况下接收缓冲大小不超过 65535（TCP Header 中 Window Size 大小为 2 Bytes），发送端通过设置该选项，可以告诉接收端自身接收缓冲区实际的大小。扩大接收缓冲区的主要目的是为了提升在长胖管道（Long Fat Pipe）中通信时的吞吐量（Throughput）。管道的容量为带宽（Bandwidth）与时延（RTT）的乘积，想要尽量利用这条管道的通信容量就需要发送缓冲区大小和接收缓冲区大小的配合，对于应用层来说能不能传输（进行 write），直接原因一般是缓冲区可用空间的大小（water level）来决定的，所以要尽量合理利用缓冲区的大小。该选项在 TCP Header 的 Options 中本身占用三个字节（| kind = 3 | length = 3 | value (最大为 14 )|），其中 value 部分占用一个字节，代表的是“窗口大小”字段按位左移的位数，由于窗口自身在判断有效分节（"old" or "new"）上的一些[有效序列号检测机制](https://tools.ietf.org/html/rfc1323#page-10)，value 的最大值为 14（即实际实际窗口的最大容量约为 1GB）。
* [SACK](https://tools.ietf.org/html/rfc2018)，选择性确认（Selective Acknowledgment），主要用于提高重传的效率，TCP 可以将滑动窗口内的数据以多个分组的形式连续发送出去（即成块的数据流），如果传输过程中有多个分组丢失，那么在每个 RTT 内发送端只能从原始的累积性确认（Cumulative Acknowledgment）中检测到一个分组的丢失，这样就加大了整体的重传的延迟。TCP 中发送端通过选择性重传，以正面确认形式告诉接收端哪些分组已经成功接收到了，这样接收端便可以只重传那些丢失了的分组，提高了重传的效率。据 [RFC2018](https://tools.ietf.org/html/rfc2018) 的描述，该选项占用两个 kind 的选项，其中 kind 为 4 的选项（TCP Sack-Permitted Option）用来发送端在连接建立（随 SYN 分节发送）时通知接收端是否支持接收选择性确认通知，以便接收端在后续可以发送选择性确认数据。kind 为 5 的选项用来通知发送端接收端已经成功接收到数据块的序列号范围，该选项可包含多个范围，每个范围被称之为一个 SACK 块，由两个 32 位整数构成（[ left SN, right SN )），n 个数据块总共占用的空间大小为 8 n + 2 ，考虑到 TCP 选项部分最多为 40 字节，理论上 n 最多为 4 ，实际最大个数还要考虑到其它选项对空间的占用情况（如时间戳选项），关于本选项的更多细节可参考[RFC2018](https://tools.ietf.org/html/rfc2018)。
* [TSOpt](http://www.ietf.org/rfc/rfc1323.txt)，时间戳选项（Timestamps Option），形式上包含两个 4 字节的时间戳字段（| kind = 8 | length = 10 | TSval( 32 bits ) | TSEcr ( 32bits ) |）。该选项典型有三个作用：
   * 1）用于区分在高速传输的网络上（MSL 时间内）两个具有相同序列号（TCP 首部中的序列号）的 TCP 分节，一个为旧的重复分节，一个为最近新产生的分节，旧的分节可能属于同一个连接（五元组）之前的化身（incarnation），它可以被看作为一个对原始序列号的 32 位（TSval 字段）扩展，作用上被称之为防回绕序列号（PAWS），[RFC1185](https://tools.ietf.org/html/rfc1185)中对于高速传输的链路中的一些问题做了更详细的描述。
   * 2）用于估算 RTT，接收端借助发送端所 ACKed 的 TCP 分节中的时间戳回显数值（TSEcr 字段）以及当前时间戳，便可以计算出相应数据包的 RTT，可以为 [RTO](https://tools.ietf.org/search/rfc6298) 的计算提供更精准的样本。
   * 3）对于服务端可配合 [SYN Cookie](https://en.wikipedia.org/wiki/SYN_cookies)  功能使用，当触发 tcp_syncookies 功能时可用于缓存客户端 SYN 分节中所通告的 MSS 、WS 、SACK 等 TCP 选项的数值，以供后续连接成功建立时，恢复原始选项的数值，方便正常通信使用。

##### 6）TCP 状态
在 TCP 连接的整个生存期间，通信的端点（TCP Socket）会随着通信的握手、建立、关闭等过程出现一系列的状态转变，细分的话可以分为12种状态，整个的状态转换过程便组成了 TCP 的[状态机](https://tools.ietf.org/html/rfc793#page-23)。

* TIME_WAIT，[RFC793](https://tools.ietf.org/html/rfc793#page-22) 对于 TIME_WAIT 的解释为：“represents waiting for enough time to pass to be sure the remote TCP received the acknowledgment of its connection termination request”，该状态出现在 TCP 连接正常断开时主动关闭的一端（即率先发送 FIN 分节的一端，如果是同时关闭，则两端最终都会处于 TIME_WAIT 状态）。当处于 FIN_WAIT2 状态（如果是同时关闭，则经由的过程是 FIN_WAIT1 -> CLOSING -> TIME_WAIT）的套接字在接收到 FIN 分节之后便会转变为 TIME_WAIT 状态，处于 TIME_WAIT 状态的套接字将不能再接收数据分节，其典型的作用有两个：

   * 1）用于支持 TCP 连接双向可靠地断开，因为在 TCP 连接正常断开时的四次挥手过程中主动关闭的一端会向对端发送最后的一路 ACK 分节，如果该路的 ACK 分节出现丢失或接收端检测到超时（重传 FIN ），主动关闭端需要能够正确的进行重传（前文曾提到任何消耗 SN 的分节都需要 TCP 的重传机制进行可靠性保障），处于 TIME_WAIT 状态的 socket 可以满足这种需求。
   * 2）避免采用相同五元组连接的新的化身受到旧的化身中的分组（lost duplicate packets or wandering duplicate packets）的干扰。为了满足这些要求，TIME_WAIT状态至少要保持 2MSL（RFC793 建议 MSL 为 120s，Linux下典型的时长为 60s） 长的时间。这里考虑下正常情况下的通信（即四路挥手正常进行），处于 TIME_WAIT 状态的套接字首次发送的 ACK（不考虑丢失）最长经过 MSL 长的时间到达处于 LAST_ACK 状态的端点，在 ACK 到达 LAST_ACK 端点前，LAST_ACK 端点有可能重传 FIN 分节（超时重传），重传的 FIN 分节发的越晚，那么该分节在网络中失效的时间点出现的也就越晚。这个较晚的重传时间点就是当发送端的 ACK 在临界点时间（用时 MSL）到达时出现，而为了处理重传的 FIN，TIME_WAIT 状态的套接字需要再等待 MSL 长的时间（累计 2 MSL 时长），当接收到重传的 FIN（最迟在 2 MSL 时刻收到）之后，会再次发送 ACK，并[重新启动 2MSL 时长的计时器](https://tools.ietf.org/html/rfc793#page-73)。如果未重启计时器或 TIME_WAIT 状态停留的时间较短，那么当 LAST_ACK 端未收到最后的 ACK 触发超时重传 FIN 时，当重传的 FIN 到达对端时，对端的 TIME_WAIT 状态已经结束，于是响应 LAST_ACK 端以 RST，在 RST 到达 LAST_ACK 端之前（距离上次发送 FIN 最多又经历 2 MSL）再次触发超时发送 FIN ，重传 FIN 之后又刚好收到 RST，链接被销毁，如果在最后的那个 FIN 未到达对端时的生存期内，收到 RST 的一端又与对端建立一个重用之前五元组的一个新的连接，那么刚刚的那个 FIN（处于 MSL 时间内的老的分组）却有可能会再次成为新连接中的干扰分组。不过整个过程可以同时满足这些情况的特殊构造出现的概率会很低，因为不仅要通过合适的时机触发相应的分节，同时也需要老的分组的序列号等也要满足新连接中序列号范围的要求，再加上如果启用 PAWS ，整个情形就更难出现了。
      
  从应用的角度来看（user 层面）处于 TIME_WAIT 状态的套接字已经不再占用资源了（不再参与正常的逻辑通信），因为 TIME_WAIT 对应的连接已经在应用中被双向关闭了（fully closed）掉了。不过从系统的角度来看（kernel 层面），处于 TIME_WAIT 状态的连接仍占用一定的系统资源，在 memory 层面上，占用 “[struct tcp_timewait_sock](http://lxr.free-electrons.com/source/include/linux/tcp.h#L396)” 资源，用于维护 TIME_WAIT 状态的存活，以及超时后的资源释放。对于出站连接（outgoing connection），即客户端对应的 TIME_WAIT，内核还需要维护 “[struct inet_bind_bucket](http://lxr.free-electrons.com/source/include/net/inet_hashtables.h#L77)” 资源的占用，两种结构的资源大概占用了 216 字节，不过相对于正常通信状态时的 “[struct tcp_sock](http://lxr.free-electrons.com/source/include/linux/tcp.h#L144)” 资源占用的大小（1776 字节）要小了很多。cpu 层面的消耗主要在出站连接上（客户端），在动态获取需要绑定的端口 [inet_csk_get_port](http://lxr.free-electrons.com/source/net/ipv4/inet_connection_sock.c#L97) 过程中在检测bind冲突时会出现一定时间的自旋（require spin-lock），整体来说短时间内建立出站连接请求较多并且当前可用的端口较少时，可能会存在一定时间的 CPU 阻塞。

   考虑到 TIME_WAIT 状态的套接字的主要缺点：默认情况下至少在 2MSL 时间内不能被使用相同五元组的新的连接化身复用。当处于 TIME_WAIT 状态的套接字较多时，会降低系统对应用的服务提供能力（这里客户端受的影响可能比服务端更明显）。常用的一些用于快速回收、重用 TIME_WAIT 状态套接字的一些方法一般基于以下两条描述：
   
    * 1） [RFC1122](https://tools.ietf.org/html/rfc1122#page-88)  中有提到对于处于 TIME_WAIT 状态的套接字如果新收到的 SYN 分节中的 SEQ 大于之前同一个连接的较早化身（incarnation）所使用的最大的 SEQ 时，那么它便可以重新打开（reopen）这个连接。
	 * 2） [RFC6191](https://tools.ietf.org/html/rfc6191) 描述了处于 TIME_WAIT 状态的套接字也可以通过时间戳选项（用于避免和老连接实例的报文段出现混淆）用于新连接的建立。

   结合以上两点，在 Linux 中处于 TIME_WAIT 状态的套接字收到SYN分节后的处理在接口 [tcp_timewait_state_process](http://lxr.free-electrons.com/source/net/ipv4/tcp_minisocks.c#L226) 中，在 Linux 中关于 TIME_WAIT 状态的转变、重新打开、计时器重置等维护工作更详细的参考在[这里](http://lxr.free-electrons.com/source/net/ipv4/tcp_ipv4.c#L1762)。

   关于TIME_WAIT状态套接字的快速回收和重用，Linux下的方式有：	
	
   * 1）TIME_WAIT快速回收，通过开启内核参数 net.ipv4.tcp_tw_recycle 和 net.ipv4.tcp_timestamps（一般默认下已开启）来开启对于 TIME_WAIT 状态套接字的快速回收功能。该功能即影响 outgoing connection（[tcp_v4_connect](http://lxr.free-electrons.com/source/net/ipv4/tcp_ipv4.c?v=2.6.35#L198)） 也影响 incoming connection（[tcp_v4_conn_request](http://lxr.free-electrons.com/source/net/ipv4/tcp_ipv4.c?v=2.6.35#L1347)）。开启后，TIME_WAIT 状态停留的时间将变为 3.5RTO（即 1 + 2 + 0.5，该数值需大于 TS 变化的时钟粒度，考虑到 RTO 退避，可以满足对端重传 FIN 两次，[RFC6298](https://tools.ietf.org/html/rfc6298#page-3)  建议 RTO 最小为 1s，最大值至少为 60s，不过 Linux 下可以通过更细的时钟粒度进行 RTT 采样，RTO 最小一般为 200ms，最大值为 120s）。当进入 TIME_WAIT 状态之后，内核会记录最近一次对端所通知的时间戳（SEG.TSval），这个时间戳会被[记录](http://lxr.free-electrons.com/source/net/ipv4/tcp_minisocks.c#L158)在一个包含各种指标的特定的[数据结构](http://lxr.free-electrons.com/source/net/ipv4/tcp_metrics.c#L42)中。当 TIME_WAIT 状态被回收之前在收到数据时的处理流程可参考前文提到的 [tcp_timewait_state_process](http://lxr.free-electrons.com/source/net/ipv4/tcp_minisocks.c#L226) 接口。当 TIME_WAIT socket 被快速回收之后，为了可以达到处于 2MSL 等待中的 TIME_WAIT socket 所能提供的效果，对于 server 端来说，将会丢弃（drop）掉同时满足以下三个条件的连接请求：
   
      *  来自同一个 host（连接被快速释放了，度量信息中只包含了 saddr 、daddr、以及时间戳相关的信息） 的 TCP 包携带了时间戳。 
		*  之前同一台  host  在 [TCP_PAWS_MSL](http://lxr.free-electrons.com/source/include/net/tcp.h?v=2.6.35#L146) 秒之内与本端 Server 有过 TCP 通信。
		*  新的连接请求中的时间戳出现回退，即小于 server 端记录的时间戳并且差值大于 [TCP_PAWS_WINDOW](http://lxr.free-electrons.com/source/include/net/tcp.h?v=2.6.35#L152)（该数值应该小于 TIME_WAIT 状态最小的存留时间）。
  
      对于 client 端来说，在 [TCP_PAWS_MSL](http://lxr.free-electrons.com/source/include/net/tcp.h#L159) 时间内，对于相同的目的 host 发出连接请求时，会通过之前记录的时间戳[重新构造](http://lxr.free-electrons.com/source/net/ipv4/tcp_ipv4.c#L201) “最近一次收到对端通知的时间戳”，以供后续支持 PAWS 功能使用。

      对于 server 端一般不建议开启 net.ipv4.tcp_tw_recycle 选项，因为 server 和 client 端的连接可能需要通过一些对 TCP 状态有感知的节点：NAT 设备、防火墙、负载均衡器等。比如当外出连接通过 NAT 装置时，后方的多台 host 可能会被映射为同一个外出 IP，而不同的 host 之间关于时间戳的变化会存在时钟差异，这样新发出的连接到达 server 之后可能会影响到 server 端的判断，使得新发出的连接被丢弃掉，客户端可能会因超时而建立连接失败。如果短时间内发往同一台 server 的外出连接比较多的时候，这种现象可能会更加明显。
   * 2）TIME_WAIT 重用，即短时间内直接重用：a）处于 TIME_WAIT 状态的连接（即连接的 reopen，和之前的连接占用相同的五元组），b）只是其中的端口用于新的连接（地址的 reuse，目的 IP 或 Port 和之前不相同），包含 client 端的 outgoing 连接和 server 端的 incoming 连接。当绑定一个处于 TIME_WAIT 状态的端口时，对于 client 端 connect 系统调用会设置 errno 为 EADDRNOTAVAIL，连接建立失败，对于服务端 listen 调用会设置 errno 为 EADDRINUSE。对于用户态，可以借助 SO_REUSEADDR 套接字选项来进行端口重用。在内核层面上，通过开启内核参数 net.ipv4.tcp_tw_reuse 和  net.ipv4.tcp_timestamps 可开启该功能，该功能主要对于客户端（outgoing connection）比较有效。另外能否对于处于 TIME_WAIT 状态的连接进行完全的重用（即完全重用相同的五元组），这会依赖于具体的实现在不同的使用场景下能否可以做到前文中 RFC1122 和 RFC6191 中所提到的需求。

   另外还有一些方法可以达到消除或避免 TIME_WAIT 状态存在的目的：
   
   * 1）RST 分节，默认情况下处于TIME_WAIT状态的套接字在收到 RST 分节后会立即被 [kill](http://lxr.free-electrons.com/source/net/ipv4/tcp_minisocks.c#L261) 掉 ，即前文所指的 [TWA](https://tools.ietf.org/html/rfc1337#page-1)， Linux 下可通过开启 net.ipv4.tcp_rfc1337（默认下关闭） 内核参数关闭该功能 。
	* 2）SO_LINGER，通过开启该套接字选项并将 linger 时长设置为 0，那么在对套接字执行 close 操作时，协议栈将直接抛弃发送缓冲区中未发送出去的数据，并不按正常的四路挥手过程来终止相应的连接，而是发送 RST 分节给对端，非正常的终止连接，可能会出现数据包串话的危险，应用中应尽量避免这种用法。
	* 3）net.ipv4.tcp_max_tw_buckets（IPv4），Linux 下当系统中同时出现的 TIME_WAIT 状态的套接字数量达到该限制之后，对于新出现的 TIME_WAIT socket 系统将会直接将其销毁掉，实现中是在分配  [inet_timewait_sock](http://lxr.free-electrons.com/source/include/net/inet_timewait_sock.h#L49) 结构时当检测到达到系统上限之后，便会按[分配失败处理](http://lxr.free-electrons.com/source/net/ipv4/inet_timewait_sock.c#L156)。该数值默认是起到一定的抵抗DOS攻击的作用，官方文档建议不要人为的减少该选项的数值，而是应根据实际的需要以及系统的内存量适当的调大该数值。

* FIN_WAIT2，TCP 在执行正常四路握手时 TIME_WAIT 前的一个状态，对于主动执行关闭的一端，如果执行的是全关闭（fully close，本端的发送缓冲区和接收缓冲区都会收到影响），在接收到对端的 ACK 之后，会由 FIN_WAIT1 转变为 FIN_WAIT2 ，此时该状态不再和任何应用层程序产生关联，该连接属于是孤儿连接（orphaned connection），由内核控制。在进一步接收到对端的 FIN 分节之后会由 FIN_WAIT2 状态转变为 TIME_WAIT 状态并启动 2MSL 计时器。在特殊情况下如果对端迟迟不发送最后的 FIN 分节，或者一直接收不到对端通告的 FIN 分节，那么 FIN_WAIT2 端有可能一直处于这个状态，使得连接占用的资源一直未被释放。为了避免 FIN_WAIT2 状态的无限等待，在本端执行全关闭的情况下，Linux 下会设置一个计时器，如果计时器超时后连接仍是空闲的，那么连接将会转变为 CLOSED（一个虚拟的状态） 状态并被销毁。计时器时长（秒）由内核参数 net.ipv4.tcp_fin_timeout（默认值60） 控制。如果通信端执行的是半关闭 （half close，如 shutdown（SHUT_WR） ，此时影响的只是发送端的发送缓冲，连接属于 “ un-orphaned connection”，处于 “receive only” 状态），那么 FIN_WAIT2 状态将不会通过内核的计时器进行超时销毁。实践中为了避免对端应用一直不关闭连接，可以通过在应用层设置计时器在超时后再执行全关闭。

 To be continued.......

<!--
	##### TCP 套接字选项
	 [ECN-steup SYN/SYN-ACK](https://tools.ietf.org/html/rfc3168#page-15)
-->
