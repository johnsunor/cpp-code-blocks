
本部分源码为基于[KCP](https://github.com/skywind3000/kcp)协议所做的一些实践

[TOC]

###关于[TCP](http://man7.org/linux/man-pages/man7/tcp.7.html)                                                                                                                                                                
TCP是在网络层(IP)的基础上为通信双方提供了可靠的、面向流的、全双工的、基于连接的通信方式，其核心有三点：尽量而为的可靠、流量控制和拥塞控制。

####1. [TCP的可靠性](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Reliable_transmission)
TCP的可靠性传送在信息论与编码论中，属于是一种“尽力而为”的行为，即通过简单的尝试重复发送（ARQ）来进行尽力投递（delivery），在实现中主要通过下列方式来提供可靠性：
> * 积极确认（positive acknowledgement）
> * 丢失分组重传（timeout based retransmission & dupack based retransmission）
> * 重复分组检测（duplicate packets discard）
> * 乱序分组重排（reconstructed in order）
> * 错误检测（end-to-end checksum, weak check）

####2. [TCP的流量控制](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Flow_control)
TCP使用端到端的流量控制方式，主要用于避免数据在发送和接收上出现速度失配，行为上主要受滑动窗口来控制，滑动窗口的大小一般由接收方通告（offer），窗口大小的调控需考虑通信管道的容量（带宽与时延的乘积，即bandwidth X rtt），不能过大（可能会增加数据传送的延迟），也不能过小（不利于吞吐量）。除此之外，发送端用于实现拥塞避免的拥塞窗口也控制着发送端数据发送行为，以及一般为了避免网络中小分组过多的nagle算法和为了利用捎带（piggy）机制的dalayed-ack算法也起到了一定的流量控制行为。
> * 滑动窗口式流量控制（[sliding window](https://en.wikipedia.org/wiki/Sliding_window_protocol)）
> * 用于拥塞避免的拥塞窗口（[congestion window](https://en.wikipedia.org/wiki/TCP_congestion_control#Congestion_window)）
> * 用于提高网络利用率的[tcp-nagle](https://en.wikipedia.org/wiki/Nagle's_algorithm)算法和[delayed-ack](https://en.wikipedia.org/wiki/TCP_delayed_acknowledgment)算法

####3. [TCP的拥塞控制](https://en.wikipedia.org/wiki/TCP_congestion_control)
TCP的拥塞控制主要用于控制数据进入网络的速率，避免网络拥堵，提高网络传输整体的性能。因为通信状况是动态变化的，所以下面所列出的各项机制往往是在通信过程中是动态的相互配合使用的。传统的TCP实现对于网络拥塞的感知是以丢包为信号的，当丢包发生时，会分析丢包的原因（超时或快速重传），然后触发不同的机制（慢启动或拥塞避免）来进行拥塞控制（收敛拥塞窗口和慢启动门限）。抑或借助网络设备（一般受限于网络设备的部署及更新换代）对于拥塞的感知（路由器的ECN能力）来在丢包出现前提前进行拥塞控制。不过随着网络通信的发展，基于丢包的拥塞控制算法已经有点过时了，有时并不能反馈真实的网络拥塞情况，对于丢包的过渡反馈以及典型的[bufferboat](https://en.wikipedia.org/wiki/Bufferbloat)问题会使吞吐量和延迟受到明显的影响。Google开源的[BBR](https://queue.acm.org/detail.cfm?id=3022184)(基于拥塞的拥塞的控制)算法，在实践中对于吞吐量的提高和延迟的降低都取得了不错的效果。
> * 慢启动[slow start](https://en.wikipedia.org/wiki/TCP_congestion_control#Slow_start)
> * 拥塞避免[congestion window](https://en.wikipedia.org/wiki/Congestion_window)
> * 用于激活快速恢复算法[fast recovery](http://www.isi.edu/nsnam/DIRECTED_RESEARCH/DR_WANIDA/DR/JavisInActionFastRecoveryFrame.html)的快速重传算法[fast retransmit](https://en.wikipedia.org/wiki/TCP_congestion_control#Fast_retransmit)
> * 需借助网络设备的显式拥塞通知机制[explicit congestion notification](https://en.wikipedia.org/wiki/Explicit_Congestion_Notification)

####4. 一些细节
<table class="table table-bordered table-striped table-condensed" style="margin: 0 auto; text-align: center;">
<caption><a href="https://en.wikipedia.org/wiki/Transmission_Control_Protocol">TCP</a> Header Format</caption>
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
<td colspan="16"><center>源端口</center></td>
<td colspan="16"><center>目的端口</center></td>
</tr>

<tr>
<td colspan="32"><center>序列号</center></td>
</tr>
<tr>

<td colspan="32"><center>确认号</center></td>
</tr>
<tr>

<td colspan="4"><center><br/>头部长度</center></td>
<td colspan="3"><center>保留位</center><br />
<tt><b><center>0 0 0</center></b></tt></td>
<td><center><tt><a href="https://tools.ietf.org/html/rfc3540">N<br />
S</tt></center></a></td>
<td><center><tt><a href="https://tools.ietf.org/html/rfc3540">C<br />
W<br />
R</tt></center></a></td>
<td><center><tt><a href="https://tools.ietf.org/html/rfc3540">E<br />
C<br />
E</tt></center></a></td>
<td><center><tt>U<br />
R<br />
G</tt><center></td>
<td><center><tt>A<br />
C<br />
K</tt><center></td>
<td><center><tt>P<br />
S<br />
H</tt></center></td>
<td><center><tt>R<br />
S<br />
T</tt></center></td>
<td><center><tt>S<br />
Y<br />
N</tt></center></td>
<td><center><tt>F<br />
I<br />
N</tt></center></td>
<td colspan="16"><center><br/>窗口大小</center></td>
</tr>
<tr>
<td colspan="16"><center>校验和</center></td>
<td colspan="16"><center>紧急指针</td>
</tr>
<tr>
</tt></th>
<td colspan="32" style="background:#ffd0d0;"><center>TCP选项(最多40字节)<br />
...</center></td>
</tr>
</table>

<table class="table table-bordered table-striped table-condensed" style="margin: 0 auto;text-align: center;">
<caption><a href="https://en.wikipedia.org/wiki/IPv4">IPv4</a> Header Format</caption>
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
<td colspan="4"><center>版本号</center></td>
<td colspan="4"><center>首部长度</center></td>
<td colspan="6"><center>区分服务码点</center></td>
<td colspan="2"><center><a href="https://tools.ietf.org/html/rfc3168">ECN</a></center></td>
<td colspan="16"><center>IP包整体长度</center></td>
</tr>

<tr>
<td colspan="16"><center>片标示</center></td>
<td colspan="3"><center>分片标志</center></td>
<td colspan="13"><center>13位片偏移</center></td>
</tr>

<tr>
<td colspan="8"><center>生存期</center></td>
<td colspan="8"><center>传输层协议号</center></td>
<td colspan="16"><center>首部校验和</center></td>
</tr>

<tr>
<td colspan="32"><center>源IP地址</center></td>
</tr>
<tr>
<td colspan="32"><center>目的IP地址</center></td>
</tr>
<tr>
<td colspan="32" rowspan="4"><center>IP选项（最多40字节）<br />...</center></td>
</tr>

</table>

<table class="table table-bordered table-striped table-condensed" style="margin: 0 auto;text-align: center;">
<caption>TCP pseudo-header for checksum computation (IPv4)</caption>
<tr>

<th colspan="4" style="width:11%;"><center>0–3</center></th>
<th colspan="4" style="width:11%;"><center>4–7</center></th>
<th colspan="8" style="width:22%;"><center>8–15</center></th>
<th colspan="16" style="width:44%;"><center>16–31</center></th>
</tr>
<tr>

<td colspan="32" style="background:#fdd;"><center>源地址</center></td>
</tr>
<tr>
<td colspan="32" style="background:#fdd;"><center>目的地址</center></td>
</tr>

<tr>
<td colspan="8" style="background:#fdd;"><center>零</center></td>
<td colspan="8" style="background:#fdd;"><center>协议类型（17）</center></td>
<td colspan="16" style="background:#fdd;"><center>TCP分节长度</center></td>
</tr>

</table>

#####1）[三次握手](http://www.tcpipguide.com/free/t_TCPConnectionEstablishmentProcessTheThreeWayHandsh-3.htm)
>TCP连接建立所需要的三次握手，是出于在不完全可靠的信道中进行通信（[Two_Generals'_Problem](https://en.wikipedia.org/wiki/Two_Generals%27_Problem)）时通信双方就某一问题达成一致所需要的最少交互要求。不过在实际通信时也有可能出现四次握手的情形（连接同时打开（[simultaneous open](http://www.tcpipguide.com/free/t_TCPConnectionEstablishmentProcessTheThreeWayHandsh-4.htm)））。同时在连接建立的过程中双方也会相互通告各自的ISN、Sindow Size（用于对端建立滑动窗口），TCP Options等，并完成各自通信所需资源的分配。

#####2）[四次挥手](http://www.tcpipguide.com/free/t_TCPConnectionTermination-2.htm)
>TCP连接断开时需要的四次挥手，相比连接建立时多出来的一次挥手，是因为[half-close](http://www.vorlesungen.uni-osnabrueck.de/informatik/networking-programming/notes/22Nov96/3.html)的存在，不过在通信中（如Linux下）也有可能会出现三次挥手就完成连接的断开（当被动关闭端没有后续数据要发送时可能将ACK和FIN分节[合并](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Connection_termination)。

#####3）序列号
>32位的序列号用于标示TCP发送端到接收端的数据流中的一个字节（TCP分节中的首个字节），SYN分节中包含着发送端的ISN，FIN分节包含着发送端的LSN。在TCP中凡是消耗SN（使SN有效递增）的分节（SYN、FIN和带有payload的分节）都需要重传来提供可靠性保障。ISN的生成需要考虑一定的[安全](https://tools.ietf.org/html/rfc1948)因素（被攻击者guess到之后，可以向连接中注入伪造的数据包进行攻击），另外一个需要考虑的因素是防回绕（Protection Against Wrapped Sequence Numbers），在[Linux2.6.39](http://lxr.free-electrons.com/source/drivers/char/random.c?v=2.6.39#L1528)中典型的实现是通过结合通信双方的IP，Port构成的四元组进行散列以及一个动态变化的时钟（每64ns加1，32位无符号数环绕一次周期约为274秒，这个周期也避免了网络中处于MSL（30s~2min，Linux下典型值为30s）内旧的无效的分组和TCP新产生的分组间的干扰）结合生成的。关于序列号的防回绕，另一个可以用的机制是TCP的[时间戳选项](https://tools.ietf.org/html/rfc7323)，后文会有更详细的介绍。

#####4）TCP-Flags
>一些老的TCP实现中只能理解其中的后6位flags，前三个主要和拥塞控制有关。
 　　a）[NS](https://tools.ietf.org/html/rfc3540)，随机和（Nonce Sum），这是一个实验性的flag，主要目的是给TCP通信中的发送端（往往是服务端）一种机制来检查接收端的一些异常行为（恶意的（如接收端TCP进行[乐观ACK攻击](http://www.kb.cert.org/vuls/id/102014)或故意移除ECE标志）或非恶意的（如通信链路中的NAT设备，Router，Firewall等可能由于不支持等原因将ECE标志清楚）），异常的接收端可能会占用更多的带宽，破坏掉网络通信中带宽占用的公平性，提高自身的优先级。
　　b）[CWR](https://tools.ietf.org/html/rfc3540)，发送端拥塞窗口减小（Congestion window reduced），一般当发送端收到[ECE](https://tools.ietf.org/html/rfc3540)标志后会设置该选项，来通知对端，该选项需配合ECE标志和IP Header中的ECN bits协调使用，同时也需要通信两端以及通信链路中的设备支持才能起到特定的作用。
　　c）[ECE](https://tools.ietf.org/html/rfc3540)，ECN回显（ECN Echo），当通信中的接收端收到链路对于拥塞的感知（即IP Header中ECN bits被设置，如路由器感知到链路拥塞后设置）标志后，会将后续的TCP分节中设置ECE标志来通知发送端来提前进行拥塞控制，发送端收到ECE标志后会继而收敛CWN的东西，并设置CWR标志以通知接收端。这些协作机制可以避免网络中不必要的拥堵以及丢包，从而提升网络的性能。
　　d）RST，重置（Reset）

1）TCP的[校验和](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_checksum_for_IPv4)（以IPV4为例），TCP的校验和是一种端到端的校验和，计算方式为以16bits为单位的反码和的反码，计算时包含12字节伪头部、最多60字节的首部、理论上最多65515字节的数据，并在需要时用0字节将总长度填充为偶数长），。TCP的校验和是一种弱校验（weak check），数据在传输过程中如果出现双字节比特位反转，那么通过校验和将不能检测出这种错误。

###关于[UDP](http://man7.org/linux/man-pages/man7/udp.7.html)


[^_^]:



