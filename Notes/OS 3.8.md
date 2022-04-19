# OS 3.8

## 一、用户态与系统态

​		**CPU进入系统态的方式：**

​		1.中断：中断由外部设备发起，由系统接受后CPU自动进入系统状态，并从另一空间开始执行指令。中断在指令之间发生，不影响单条指令的执行。

​		2.错误：指令执行失败，就会引起错误，CPU进入系统态，并且对错误进行处理；此时执行的指令被放弃了。

​		3.自陷：CPU自行陷入系统态中处理问题

​		**系统调用：**

​		用户可以使用`system call`来使用系统调用，方便用户对程序的使用。

​		**异常：**

|     类别     |          原因           |    返回行为    |        例子        |
| :----------: | :---------------------: | :------------: | :----------------: |
| 中断（异步） | 来自I/O设备的信号或变化 | 返回下一条指令 | IRQ中断、电源掉电  |
| 陷阱（异步） |    程序内部有意设置     | 返回下一条指令 | 系统调用、信号机制 |
| 故障（同步） |    潜在可恢复的错误     |  返回当前指令  | 除0错误、段错误等  |
| 终止（同步） |     不可恢复的错误      |  不再指令指令  |    发现硬件错误    |

## 二、虚拟机结构

​		虚拟机的基本思想：系统应当提供多道程序能力以及丰富的扩展界面。不过最初的虚拟机设定在大型计算机上(因为当时PC的能力尚弱)，这样的公用计算机希望将不同用户之间的数据进行隔离，其需要较大的内存。现在，随着硬件资源的丰富，PC上也可以安装虚拟机。目前比较常用的虚拟机有Linux、Unix等。

​		**微内核结构：**内核中只包括中断处理、进程通信、基本调度等，可以完成文件系统、网络功能、内存管理的操作。其优点为易于实现、可移植性号且配置灵活。其缺点为经过了内核转发，会造成通讯的损耗。

​		**Linux哲学——将机制与策略分离：**更直接的来讲，就是将接口和引擎进行分离。例如餐馆的运营——将菜单与后厨进行分离。