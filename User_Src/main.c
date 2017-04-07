/*
Union fly 四轴

平台：STM32F103C8
支持2.4G遥控，暂不支持蓝牙
串级PID双环路控制（角速度环和角度环）
编译环境：Keil MDK 4.71.2.0及以上
------------------------------------
*/
#include "SysConfig.h"
#include "config.h"   
#include "imu.h"
#include "Altitude.h"
#include "CommApp.h"
#include "CommPC.h"
#include "ConfigTable.h"
#include "IMUSO3.h"
#include "control.h"
#include "FailSafe.h"
 
//sw counter
uint16_t  batCnt; 
//check executing time and period in different loop
uint32_t startTime[5],execTime[5];
uint32_t realExecPrd[5];	//us , real called period in different loop
 
/********************************************
              飞控主函数入口
功能：
1.初始化各个硬件
2.初始化系统参数
********************************************/
int main(void)
{

	SystemClock_HSE(9);           //系统时钟初始化，时钟源外部晶振HSEs  8*9=72MHz;
	cycleCounterInit();				// Init cycle counter
	SysTick_Config(SystemCoreClock / 1000);	//SysTick开启系统tick定时器并初始化其中断，1ms

	UART1_init(SysClock,BT_BAUD_Set); //串口1初始化
	
  NVIC_INIT();	                //中断初始化

  STMFLASH_Unlock();            //内部flash解锁

  LoadParamsFromEEPROM();

  LedInit();	                //IO初始化

  BT_PowerInit();               //蓝牙电源初始化完成，默认关闭
  MotorInit();	                //马达初始化
  BatteryCheckInit();           //电池电压监测初始化
  IIC_Init();                   //IIC初始化
	
	#ifdef IMU_SW										//使用软件解算
	MPU6050_initialize();
	#else
  MPU6050_DMP_Initialize();     //初始化DMP引擎
	#endif
	
  //HMC5883L_SetUp();           //初始化磁力计HMC5883L

  NRF24L01_INIT();              //NRF24L01初始化
	
  PowerOn();                    //开机等待
  BT_ATcmdWrite();              //蓝牙写配置
 
	BatteryCheck();

	MS5611_Init();

	IMU_Init();			// sample rate and cutoff freq.  sample rate is too low now due to using dmp.

#ifdef UART_DEBUG
	//定时器3初始化，串口调试信息输出
	TIM3_Init(SysClock,2000);
#endif

	//定时器4初始化，用于飞控主循环基准定时
	TIM4_Init(SysClock,1000);	    
	
	MotorPwmFlash(10,10,10,10);
		
	altCtrlMode=MANUAL;
	WaitBaroInitOffset();		//等待气压初始化高度完成
	
	//飞控控制主循环
  while (1)
  {
		/*Use DMP in MPU6050 for imu , it's accurate but slow and time costing and time unstable */
 		//special freq for dmp. 1000/7. use 3-5ms if normal
		//if miss time becasue of other long time task, dmp maybe  need to use 10ms
		//Crazepony默认使用软件解算
		#ifndef IMU_SW
		#ifdef DEBUG_NEW_CTRL_PRD
		if(anyCnt>=7)	//take about 3ms, to develop a faster control
		#else
		//it will take about 9ms to read. since this prd 5ms is as same as the set outpur rate in dmp.
		//which fit to old control
		if(anyCnt>=5) 
		#endif
		{
				anyCnt=0;
				realExecPrd[0]=micros()-startTime[0];
				startTime[0]=micros();
 
				DMP_Routing();	        //DMP 线程  所有的数据都在这里更新
				DMP_getYawPitchRoll();  //读取 姿态角
 
				execTime[0]=micros()-startTime[0];	//测量任务执行时间，CPU占用率

		}
		#endif
		
		//100Hz Loop
		//Crazepony默认使用100Hz的控制频率
		if(loop100HzCnt>=10)
		{
				loop100HzCnt=0;
				
				realExecPrd[1]=micros()-startTime[1];
				startTime[1]=micros();
				
				#ifdef IMU_SW
				IMUSO3Thread();
				#else
				IMU_Process();		 
				#endif
				accUpdated=1;
			
				//气压读取
				MS5611_ThreadNew();		//FSM, take aboue 0.5ms some time

				//imu校准
				if(imuCaliFlag)
				{
						if(IMU_Calibrate())
						{
							imuCaliFlag=0;
							gParamsSaveEEPROMRequset=1;	//请求记录到EEPROM
							imu.caliPass=1;
						}
				} 
				
				CtrlAttiRate();
				CtrlMotor();

				execTime[1]=micros()-startTime[1];
		}
		
		//Need to recieve 2401 RC instantly so as to clear reg.
		Nrf_Irq();
		
		//50Hz Loop
		if(loop50HzFlag)
		{
				loop50HzFlag=0;
				realExecPrd[3]=micros()-startTime[3];
				startTime[3]=micros();
				
				RCDataProcess();
			  
				FlightModeFSMSimple();
				
				//DetectLand();
				if(altCtrlMode==LANDING)	 
				{	  
						AutoLand();
				}
				
		 		AltitudeCombineThread();

				CtrlAlti();		 

				CtrlAttiAng();	 

			  //PC Monitor
#ifndef UART_DEBUG
				if(btSrc!=SRC_APP){
					//CommPCUploadHandle();	//tobe improved inside
				}
#endif
				
				execTime[3]=micros()-startTime[3];
		}
		
		//10Hz loop
		if(loop10HzFlag)
		{
				loop10HzFlag=0; 
				realExecPrd[2]=micros()-startTime[2];
				startTime[2]=micros(); 
			
				//Check battery every BAT_CHK_PRD ms
				if((++batCnt) * 100 >=BAT_CHK_PRD) 
				{
					batCnt=0; 
					BatteryCheck();
				}
				
				//手机APP有请求飞控信息，则发送给手机APP
				if(flyLogApp)	
				{
					CommAppUpload();
					flyLogApp=0;
				}
				
				//EEPROM Conifg Table request to write. 
				if(gParamsSaveEEPROMRequset)
				{
						gParamsSaveEEPROMRequset=0;
						SaveParamsToEEPROM();
				}

				FailSafeLostRC();
				
				FailSafeCrash();
				
				FailSafeLEDAlarm();	 
				
				LEDFSM();			//闪烁
				
				execTime[2]=micros()-startTime[2];
		}
 		
		//pc cmd process. need to return as quickly as ps
#ifndef UART_DEBUG
		if(pcCmdFlag)
		{
				pcCmdFlag=0;
				CommPCProcessCmd();
		}
#endif
  }//end of while(1)
}

