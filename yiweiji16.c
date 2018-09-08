/**
 * @file operating.c
 * @date 2018/07/13 18:24
 *
 * @author QiDaXia
 * @Contact: 1176201157@qq.com
 *
 * @brief:

 * @description:
 *
 * @note:

   熔丝位：FE D9 FF
*/

#include "designed.h"
#include "hooks.h"
#include "soft.h"
#include "powerManager.h"
#include "usart.h"
#include "TLC5615.h"
#include "ad.h"

int main(void)
{
	CMD cmd = STOP;
	u8 i = 0;
	u16 adcVal = 0;
	u16 chargeCount = 0;//充电计时
	u16 frequencyCount = 0;//充电上限频率统计
	delay_ms(500);//Ensure power supply
	ioInit();
	usartInit(19200);
	SPI_MasterInit();
	powerManageCFG();
	watchDog_init();
	adc_init();

	speed.MaxSpeed_lift = 1022;
	//507：正常运行速度
	//speed.MaxSpeed_walk = 507;//行走速度调节，1022对应最大速度
	//现在为了调试，将速度调至最大
	speed.MaxSpeed_walk = 1024;
	speed.MinSpeed_lift = 100;
	speed.MinSpeed_walk = 400;
	speed.SpeedDownDelay_lift = 0;
	speed.SpeedDownDelay_walk = 0;
	speed.SpeedUpDelay_lift = 1;
	speed.SpeedUpDelay_walk = 1;
	speed.BrakeReleaseDelay = 0;

	energy.contact = FALSE;
	energy.Current_energy = 0;
	//Only applicable to 16.1
	energy.Threshole_bottom = 856;//59%
	energy.Threshole_top = 901;//97%

	//
	delay_ms(3000);
	for (i = 0; i < 20; i++)
	{
		get_adc();
		delay_ms(20);
	}
	onceBeep();


	while (TRUE)
	{
		/*!
		* @brief:  按键控制
		*/
		if (IsKeyPress())
		{
			_WDR();
			keyDown();
			switch (keyScan())
			{
			case UP:
				handlerLiftForKey(toUp, IsLimitUp, IsUpRequest);
				break;
			case DOWN:
				handlerLiftForKey(toDown, IsLimitDown, IsDownRequest);
				break;
			case BACK:
				handlerWalkForKey(toBack, IsCharge, IsBackRequest);
				break;
			case FORWARD:
				handlerWalkForKey(toForward, IsInPlace, IsForwardRequest);
				break;
			case POWER:
				handlerPower(1);
				break;
			default:
				break;
			}
			keyOff();
		}
		/*!
		* @brief:  指令控制
		*/
		if (checkFrame(&cmd) == 0)
		{
			_WDR();
			keyDown();
			sendCMD(ack, 3);
			switch (cmd)
			{
			case UP:
				handlerLiftForCmd(UP, IsLimitUp);
				break;
			case DOWN:
				handlerLiftForCmd(DOWN, IsLimitDown);
				break;
			case FORWARD:
				handlerWalkForCmd(FORWARD, IsInPlace);
				break;
			case BACK:
				handlerWalkForCmd(BACK, IsCharge);
				break;
			case POWER:
				handlerPower(0);
				break;

				//cmd soft
			case CFG_SOFT_ADD_WALK:
				speed.SpeedUpDelay_walk++;
				echo();
				break;
			case CFG_SOFT_ADD_WALK_DOWN:
				speed.SpeedDownDelay_walk++;
				echo();
				break;
			case CFG_SOFT_ADD_LIFT:
				speed.SpeedUpDelay_lift++;
				echo();
				break;
			case CFG_SOFT_ADD_LIFT_DOWN:
				speed.SpeedDownDelay_lift++;
				echo();
				break;
			case CFG_TOPSPEED_SUB_WALK:
				speed.MaxSpeed_walk -= 5;
				echo();
				break;
			case CFG_LOWSPEED_ADD_WALK:
				speed.MinSpeed_walk += 5;
				echo();
				break;
			case CFG_TOPSPEED_SUB_LIFT:
				speed.MaxSpeed_lift -= 5;
				echo();
				break;
			case CFG_LOWSPEED_ADD_LIFT:
				speed.MinSpeed_lift += 5;
				echo();
				break;
			case ADD_BRAKE_RELEASE_DELAY:
				speed.BrakeReleaseDelay++;
				echo();
				break;
			case CFG_READ:
				echo();
				break;
				//测试能够使其运动的最小电压
			case CFG_TOMINSPEED_WALK:
				speedToMin(WALK);
				toForward();
				while (checkFrame(&cmd) != 0);
				toStop();
				break;
			case CFG_TOMINSPEED_LIFT:
				speedToMin(LIFT);
				toUp();
				while (checkFrame(&cmd) != 0);
				toStop();
				break;

				//复位，握手
			case CFG_RESTART:
				while (1);//wait for wdt restart
				break;
			case CFG_HAND:
				sendCMD(ackOk, 3);
				break;

				//电量设置
			case CFG_SET_THRESHOLD_T:
				energy.Current_energy = get_adc();
				energy.Threshole_top = energy.Current_energy;
				break;
			case CFG_SET_THRESHOLD_B:
				energy.Current_energy = get_adc();
				energy.Threshole_bottom = energy.Current_energy;
				break;
			case CFG_READ_CURRENT_ENERGY:
				energy.Current_energy = get_adc();
				echo();
				break;
				//eeprom操作
			case CFG_LOAD_PARAMS:
				readParameterAtEeprom();
				break;
			case CFG_UPLOAD_PARAMS:
				saveParameterToEeprom();
				break;
				//debug contact
			case CFG_SOFT_CONTACT:
				energy.contact = TRUE;
				echo();
				break;
			case CFG_SOFT_UNCONTACT:
				energy.contact = FALSE;
				echo();
				break;

			default:
				toStop();
				break;
			}
			//sendCMD(ackIdle, 4);
			keyOff();
		}
		/*!
		* @brief:  充电检测
		*/

		if (IsCharge())
		{
#if EN_CHARGE_MANAGE==1
			if (IsContact())//接触良好
			{
				LED_ON;
			}
			else
			{
				LED_OFF;
				//goto FINISHED;
			}

			if (chargeCount++ >= 30000)
			{
				chargeCount = 0;

				adcVal = get_adc();
				if (adcVal < energy.Threshole_bottom)
				{
					LED_CHARGE_ON;
				}
				else if (adcVal >= energy.Threshole_top)
				{
					frequencyCount++;
					if (frequencyCount >= 6)
					{
						frequencyCount = 0;
						LED_CHARGE_OFF;
					}
				}
				else if (adcVal < energy.Threshole_top)
				{
					if (frequencyCount >= 1)
					{
						frequencyCount = 0;
					}
				}
			}
#else
			LED_CHARGE_ON;
#endif
				}
		else
		{
			LED_CHARGE_OFF;
			LED_OFF;
		}
	FINISHED:
		_WDR();
		//default:all stop
		toStop();
			}
	return 0;
		}
