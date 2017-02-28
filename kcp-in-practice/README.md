###概要

本部分源码主要为基于[KCP](https://github.com/skywind3000/kcp)协议所做的一些实践，本文主要汇总了个人在TCP、UDP、KCP上的一些总结（以Linux和IPv4为主），在一些细节和有偏差的部分请以更正式的文档（RFC）、具体的实现（Linux协议栈）及相关的实践或实验为主作参考。<br>

从整体上来说KCP相当于在应用层实现了TCP的一些核心机制（可靠性、流量控制和拥塞控制），但并不负责底层数据的传输。在可靠性上面，KCP主要实现了：1）正面确认，包含累积性确认和类似于TCP中SACK的选择性确认。2）在重传上包含有超时重传和快速重传 。4）重复分组去重以及乱序分组重排。实现中并不包含端到端的校验功能，这一点可以交给底层的通信层（如使用开启校验和的UDP）或是由应用层自己来实现（如应用层可加入自己的CRC校验）。在流量控制上面，KCP提供了类似于TCP中的滑动窗口机制来实现，传送缓冲区和发送缓冲区的大小初始时由用户自己设置。在拥塞控制上面（可以由用户选择性开启），KCP也实现了拥塞窗口，借助快速重传可以激活快速恢复算法，在检测到超时丢包时，也可以触发慢启动。KCP的优势主要在于在提供可靠性的条件下，可以降低（由用户控制）对于丢包的敏感度（退避），在超时重传上用户可以降低RTO的退避，通过开启nodelay机制可以加快ack的发送，降低延迟。在有流量控制的前提下，通过关闭拥塞控制，可以避免传送速度的突降。总体来说相对于TCP在一些有折中处理机制或规避处理的环节，KCP可以以一种相对激进的方式来处理（更保守的退避），从而降低延迟，加快通信。当然其缺点也比较明显，带宽的利用率不够高（考虑TCP的nagle算法和delayed-ack机制）可能会进一步增加网络整体的拥塞。降低退避的处理，也损失了一定的公平性，侧面提升了自身通信的优先级和带宽占用率，另外一点就是底层传输上一般结合着UDP使用，不太适用于需要大流量传输的通信模式。
****

###关于[TCP](http://man7.org/linux/man-pages/man7/tcp.7.html)                                                                                                                                                                
TCP是在网络层(IP)的基础上为通信双方提供了可靠的、面向流的、全双工的、基于连接的通信方式，其核心有三点：可靠性、流量控制和拥塞控制。

####1. [TCP的可靠性](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Reliable_transmission)
TCP的可靠性传送在信息论与编码论中，属于是一种“尽力而为”的行为，即通过简单的尝试重复发送（ARQ）来进行尽力投递（delivery），在实现中主要通过下列方式来提供可靠性：
> * 正面确认（positive acknowledgement）
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
#####[TCP](https://en.wikipedia.org/wiki/Transmission_Control_Protocol) Header Format
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

#####[IPv4](https://en.wikipedia.org/wiki/IPv4) Header Format
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
<td colspan="2" align="center"><a href="https://tools.ietf.org/html/rfc3168">ECN</a></td>
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

#####TCP pseudo-header for checksum computation (IPv4)
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
<td colspan="8" style="background:#fdd;" align="center">协议类型（17）</td>
<td colspan="16" style="background:#fdd;" align="center">TCP分节长度</td>
</tr>

</table>

#####1）[三次握手](http://www.tcpipguide.com/free/t_TCPConnectionEstablishmentProcessTheThreeWayHandsh-3.htm)
TCP连接建立所需要的三次握手，是出于在不完全可靠的信道中进行通信（[Two_Generals'_Problem](https://en.wikipedia.org/wiki/Two_Generals%27_Problem)）时通信双方就某一问题达成一致所需要的最少交互要求。不过在实际通信时也有可能出现四次握手的情形（连接同时打开（[simultaneous open](http://www.tcpipguide.com/free/t_TCPConnectionEstablishmentProcessTheThreeWayHandsh-4.htm)））。同时在连接建立的过程中双方也会相互通告各自的ISN、Sindow Size（用于对端建立滑动窗口），TCP Options等，并完成各自通信所需资源的分配。

#####2）[四次挥手](http://www.tcpipguide.com/free/t_TCPConnectionTermination-2.htm)
TCP连接断开时需要的四次挥手，相比连接建立时多出来的一次挥手，是因为[half-close](http://www.vorlesungen.uni-osnabrueck.de/informatik/networking-programming/notes/22Nov96/3.html)的存在，不过在通信中（如Linux下）也有可能会出现三次挥手就完成连接的断开（当被动关闭端没有后续数据要发送时可能将ACK和FIN分（节即第二路和第三路）进行[合并](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Connection_termination)）。

#####3）序列号
32位的序列号用于标示TCP发送端到接收端的数据流中的一个字节（TCP分节中的首个字节），SYN分节中包含着发送端的ISN，FIN分节包含着发送端的LSN。在TCP中凡是消耗SN（使SN有效递增）的分节（SYN、FIN和带有payload的分节）都需要重传来提供可靠性保障。ISN的生成需要考虑一定的[安全](https://tools.ietf.org/html/rfc1948)因素（被攻击者guess到之后，可以向连接中注入伪造的数据包进行攻击），另外一个需要考虑的因素是防回绕（Protection Against Wrapped Sequence Numbers），在[Linux2.6.39](http://lxr.free-electrons.com/source/drivers/char/random.c?v=2.6.39#L1528)中典型的实现是通过结合通信双方的IP，Port构成的四元组进行散列以及一个动态变化的时钟（每64ns加1，32位无符号数环绕一次周期约为274秒，这个周期也避免了网络中处于MSL（RFC793建议值为2min，Linux下典型值为30s）内旧的无效的分组和TCP新产生的分组间的干扰）结合生成的。关于序列号的防回绕，另一个可以用的机制是TCP的[时间戳选项](https://tools.ietf.org/html/rfc7323)，后文会有更详细的介绍。

#####4）TCP-Flags
一些老的TCP实现中只能理解其中的后6位flags，前三个主要和拥塞控制有关。<br>
　　a）[NS](https://tools.ietf.org/html/rfc3540)，随机和（Nonce Sum），这是一个实验性的flag，主要目的是给TCP通信中的发送端（往往是服务端）一种机制来检查接收端的一些异常行为（恶意的（如接收端TCP进行[乐观ACK攻击](http://www.kb.cert.org/vuls/id/102014)或故意移除ECE标志）或非恶意的（如通信链路中的NAT设备，Router，Firewall等可能由于不支持等原因将ECE标志清楚）），异常的接收端可能会占用更多的带宽，破坏掉网络通信中带宽占用的公平性，提高自身的优先级。<br>
　　b）[CWR](https://tools.ietf.org/html/rfc3540)，发送端拥塞窗口减小（Congestion window reduced），一般当发送端收到[ECE](https://tools.ietf.org/html/rfc3540)标志后会设置该选项，来通知对端，该选项需配合ECE标志和IP Header中的ECN bits协调使用，同时也需要通信两端以及通信链路中的设备支持才能起到特定的作用。<br>
　　c）[ECE](https://tools.ietf.org/html/rfc3540)，ECN回显（ECN Echo），当通信中的接收端收到链路对于拥塞的感知（即IP Header中ECN bits被设置，如路由器感知到链路拥塞后设置）标志后，会将后续的TCP分节中设置ECE标志来通知发送端来提前进行拥塞控制，发送端收到ECE标志后会继而收敛CWN的东西，并设置CWR标志以通知接收端。这些协作机制可以避免网络中不必要的拥堵以及丢包，从而提升网络的性能。<br>
　　d）RST，重置（Reset），是在TCP发生错误时发送的一种分节，有三种情况下会产生RST分节：1）在未运行服务的某地址上收到SYN分节后，会相应对端以RST分节，对端在接收到RST分节后会返回ECONNREFUSED错误（hard error）。2）通信的某一端想取消一个连接（如通过设置SO_LINGER套接字选项后调用close），如果服务端在进行accept时收到RST分节（客户端取消连接），那么服务端会返回ECONNABORTED（或EPROTO）错误（soft error），服务端需忽略这种错误。在连接已建立（至少是通信的某一端认为连接还存在）情况下收到RST后，会返回ECONNRESET错误，此时对应的连接应该被销毁。3）通信的某一端收到一个不存在（至少是收到数据的这一端认为连接不存在，如该端点主机崩溃并重启（即并未走正常的TCP连接断时的四路挥手过程））的连接上的数据时会向另一端发送RST分节，此时连接也应该被销毁。
  
#####4）TCP[校验和](https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_checksum_for_IPv4)（以IPV4为例）
TCP的校验和是一种端到端的校验和，计算方式为对数据以16bits为单位进行反码和的反码计算，数据的具体内容包含12字节伪头部、最多60字节的首部、理论上最多65515字节的应用层数据，并在需要时用0字节将总长度填充为偶数长）。TCP的校验和是一种弱校验（weak check），数据在传输过程中如果出现双字节比特位反转，那么通过校验和将不能检测出这种错误，加上链路层（link layer）的校验和的一些缺点（在跨网段通信时路由器可能由于硬件错误而破坏数据）。所以，对于重要的数据在通信时一般也需要应用层提供更进一步的错误校验。

[^_^]: TODO


