## 概述
这是Unionfly微型四轴飞行器飞控代码

平台是STM32F103C8, 裸机代码，未使用实时操作系统

串级PID双环路控制（角速度环和角度环），使飞行更稳定

配合Unionfly微型四轴遥控器代码使用(2.4G控制)

该代码默认的飞行模式是无头模式。

开启SysConfig.h文件中的宏`UART_DEBUG`，可以开启USB串口的打印调试。关闭该宏，则USB口用于和上位机通信，在串口终端上只会看到乱码。
