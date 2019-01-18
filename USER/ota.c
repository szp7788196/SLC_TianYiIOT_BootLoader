#include "ota.h"
#include "stmflash.h"
#include "common.h"
#include "24cxx.h"
#include "usart.h"
#include "malloc.h"
#include "delay.h"
#include "string.h"
#include "bcxx.h"
#include "led.h"


iapfun jump2app;

u16 iapbuf[1024];
//appxaddr:Ӧ�ó������ʼ��ַ
//appbuf:Ӧ�ó���CODE.
//appsize:Ӧ�ó����С(�ֽ�).
void iap_write_appbin(u32 appxaddr,u8 *appbuf,u32 appsize, u8 flag)
{
	u16 t = 0;
	u16 temp = 0;
	static u16 data_pos = 0;
	static u16 sector_pos = 0;
	static u16 cnt = 0;

	for(t = 0; t < appsize; t += 2)
	{
		temp = ((u16)(*(appbuf + t + 1)) << 8) + (u16)(*(appbuf + t));
		iapbuf[data_pos ++] = temp;
	}
	cnt ++;
	if(data_pos == 1024)
	{
		cnt = 0;
		data_pos = 0;
		STMFLASH_Write(AppFlashAdd + (sector_pos ++) * 2048,iapbuf,1024);
	}

	if(data_pos > 0 && flag == 1)
	{
		STMFLASH_Write(AppFlashAdd + (sector_pos ++) * 2048,iapbuf,(cnt * 128 + appsize) / 2);
		data_pos = 0;
		sector_pos = 0;
		cnt = 0;
	}

	if(flag == 1)
	{
		cnt = 0;
	}
}

//��ת��Ӧ�ó����
//appxaddr:�û�������ʼ��ַ.
void iap_load_app(u32 appxaddr)
{
	jump2app = (iapfun) * (vu32*)(appxaddr + 4);	//�û��������ڶ�����Ϊ����ʼ��ַ(��λ��ַ)
	MSR_MSP(*(vu32*)appxaddr);						//��ʼ��APP��ջָ��(�û��������ĵ�һ�������ڴ��ջ����ַ)
	jump2app();										//��ת��APP.
}

//BootLoader�˳��ֳ�����BootLoader��Դ���л���
void BootLoader_ExitInit(void)
{
	TIM2->CR1 	= 0;
	TIM2->CR2 	= 0;
	TIM2->SMCR 	= 0;
	TIM2->DIER 	= 0;
	TIM2->SR 	= 0;
	TIM2->EGR 	= 0;
	TIM2->CCMR1 = 0;
	TIM2->CCMR2 = 0;
	TIM2->CCER 	= 0;
	TIM2->CNT 	= 0;
	TIM2->PSC 	= 0;
	TIM2->ARR 	= 0;
	TIM2->CCR1 	= 0;
	TIM2->CCR2 	= 0;
	TIM2->CCR3 	= 0;
	TIM2->CCR4 	= 0;
	TIM2->DCR 	= 0;
	TIM2->DMAR 	= 0;

	EXTI->IMR 	= 0;
	EXTI->EMR 	= 0;
	EXTI->RTSR 	= 0;
	EXTI->FTSR 	= 0;
	EXTI->SWIER = 0;
	EXTI->PR 	= 0;

	IWDG->KR 	= 0;
	IWDG->PR 	= 0;
	IWDG->RLR 	= 0;
	IWDG->SR 	= 0;

	__set_PRIMASK(1);
	delay_ms(10);
}

u8 NeedUpDateNewFirmWare(void)
{
	u8 ret = 0;

LOOP:
	if(ReadOTAInfo(HoldReg))
	{
		if(HaveNewFirmWare != 0xAA)	//���¹̼���Ҫ����
		{
			if(NewFirmWareAdd != 0xAA && NewFirmWareAdd != 0x55)
			{
				ResetOTAInfo(HoldReg);
				goto LOOP;
			}
			else
			{
				if(NewFirmWareAdd == 0xAA)
				{
					BootLoader_ExitInit();
					iap_load_app(APP1_FLASH_ADD);
				}
				else if(NewFirmWareAdd == 0x55)
				{
					BootLoader_ExitInit();
					iap_load_app(APP2_FLASH_ADD);
				}
			}
		}
		else						//���µĹ̼���Ҫ����
		{
			if(NewFirmWareAdd != 0xAA && NewFirmWareAdd != 0x55)
			{
				ResetOTAInfo(HoldReg);
				goto LOOP;
			}
			else
			{
				if((NewFirmWareVer <= 9999 && NewFirmWareVer > 0)\
					&& (NewFirmWareBagNum <= 896 && NewFirmWareBagNum > 0)\
					&& (LastBagByteNum <= 134 && LastBagByteNum > 0))	//�汾�źϷ� ���� �ܰ����Ϸ� ���� ĩ���ֽ����Ϸ� //128 + 2 + 4 = 134
				{
					if(NewFirmWareAdd == 0xAA)
					{
						AppFlashAdd = APP1_FLASH_ADD;
					}
					else if(NewFirmWareAdd == 0x55)
					{
						AppFlashAdd = APP2_FLASH_ADD;
					}

					ret = 0xAA;
				}
				else					//�����������κ�һ����� �����ܽ�������ģʽ
				{
					ResetOTAInfo(HoldReg);
					goto LOOP;
				}
			}
		}
	}
	else
	{
		ResetOTAInfo(HoldReg);
		goto LOOP;
	}

	return ret;
}


u8 SignalIntensity = 99;						//bg96���ź�ǿ��
char *UdpSockedId = NULL;
u8 FirmWareUpDate(void)
{
	u8 ret = 1;
	u8 res = 0;
//	u8 err_cnt = 0;
	u8 socket_state = 0;
	u8 socket = 0;
	u8 connected = 0;
	u8 got_ip = 0;
	u8 err_cnt_got_ip = 0;
	u8 err_cnt_re_init = 0;

	RE_INIT:
	bcxx_init();

	err_cnt_re_init ++;

	socket_state = 0;

	while(ret)
	{
		SignalIntensity = bcxx_get_AT_CSQ();

		switch(socket_state)
		{
			case 0:				//��ȡ����IP��ַ
				re_got:
				got_ip = bcxx_get_AT_CGPADDR(&UdpSockedId);

				if(got_ip == 1)
				{
					socket_state = 1;
					err_cnt_got_ip = 0;
				}
				else
				{
					delay_ms(2000);
					err_cnt_got_ip ++;

					if(err_cnt_got_ip >= 60)
					{
						err_cnt_got_ip = 0;

						goto RE_INIT;
					}

					goto re_got;
				}
			break;

			case 1:				//����һ��socket
				socket = bcxx_set_AT_NSCOR("STREAM", "6","0");

				if(socket <= 7)
				{
					socket_state = 2;
				}
				else
				{
					goto RE_INIT;
				}
			break;

			case 2:
				connected = bcxx_set_AT_NSOCO(socket, "103.48.232.122","8080");

				if(connected == 1)
				{
					socket_state = 3;
				}
				else
				{
					goto RE_INIT;
				}
			break;

			case 3:
				res = OnServerHandle(socket);

				if(res == 0xAA)				//�̼����������
				{
					ret = 0xAA;
					break;
				}
				else if(res != 1)			//��������:У�顢���ݳ��ȡ�������URL�ȴ���
				{
					goto RE_INIT;
				}
				else
				{
					err_cnt_re_init = 0;
				}
			break;

			default:

			break;
		}

		if(err_cnt_re_init >= 5)					//����������5��ȡ��OTA�������ϸ��汾����
		{
			if(NewFirmWareAdd == 0xAA)
			{
				NewFirmWareAdd = 0x55;
				AppFlashAdd = APP2_FLASH_ADD;
			}
			else if(NewFirmWareAdd == 0x55)
			{
				NewFirmWareAdd = 0xAA;
				AppFlashAdd = APP1_FLASH_ADD;
			}

			ResetOTAInfo(HoldReg);

			ret = 0xAA;
			break;
		}

		delay_ms(100);
	}

	return ret;
}

u8 DownLoadBuf[256];
u8 TempBuf[256];
u8 SendBuf[512];
u8 crc32_cal_buf[1024];
u16 bag_pos = 0;
//���ߴ�������
u8 OnServerHandle(u8 socket)
{
	static u8 store_times = 0;

	u8 ret = 0;
	u16 i = 0;
	u16 send_len = 0;
	u16 data_len = 0;
	u16 crc16_cal = 0;
	u16 crc16_read = 0;
	u32 crc32_cal = 0xFFFFFFFF;
	u32 crc32_read = 0;
	u32 file_len = 0;
	u16 k_num = 0;
	u16 last_k_byte_num = 0;
	u8 srtA_B[2] = {0,0};

	if(bag_pos <= (NewFirmWareBagNum - 1))
	{
		memset(DownLoadBuf,0,256);
		memset(SendBuf,0,256);
		memset(TempBuf,0,512);

		if(NewFirmWareAdd == 0xAA)
		{
			srtA_B[0] = 'A';
		}
		else if(NewFirmWareAdd == 0x55)
		{
			srtA_B[0] = 'B';
		}

//		sprintf((char *)TempBuf,"nnlightctl/hardware/A%02d.%02d%s/SLC%04d.bin",\
//					NewFirmWareVer / 100,NewFirmWareVer % 100,srtA_B,bag_pos);
		
		sprintf((char *)TempBuf,"GET /nnlightctl/hardware/SLC/V%02d.%02d%s/SLC%04d.bin HTTP/1.1\r\nHost: 103.48.232.122:8080\r\nUser-Agent: abc\r\nConnection: Keep-alive\r\nKeep-alive: timeout=60\r\n\r\n",\
					NewFirmWareVer / 100,NewFirmWareVer % 100,srtA_B,bag_pos);
		
		send_len = strlen((char *)TempBuf);

		HexToStr((char *)SendBuf, TempBuf, send_len);

		if(send_len != 0)
		{
			UsartSendString(USART1,SendBuf,send_len);

			memset(TempBuf,0,256);

			data_len = bcxx_set_AT_NSOSD(socket, send_len,(char *)SendBuf,DownLoadBuf);

			if(data_len != 0)
			{
				if(bag_pos <= (NewFirmWareBagNum - 2))
				{
					if(data_len == 130)
					{
						crc16_read = (u16)DownLoadBuf[128];//��ȡ���յ���У��ֵ
						crc16_read = crc16_read << 8;
						crc16_read = crc16_read | (u16)DownLoadBuf[129];

						crc16_cal = CRC16(DownLoadBuf,128);

						if(crc16_read == crc16_cal)
						{
							iap_write_appbin(AppFlashAdd + 128 * bag_pos,DownLoadBuf,128,0);
							bag_pos ++;

							ret = 1;
						}
						else	//��ͨ��CRC16����
						{
							ret = 2;
						}
					}
					else		//��ͨ�����ȴ���
					{
						ret = 4;
					}
				}
				else if(bag_pos == (NewFirmWareBagNum - 1))
				{
					if(data_len > 0 && data_len <= 134)
					{
						crc16_read = (u16)DownLoadBuf[data_len - 6];//��ȡ���յ���У��ֵ
						crc16_read = crc16_read << 8;
						crc16_read = crc16_read | (u16)DownLoadBuf[data_len - 5];

						crc16_cal = CRC16(DownLoadBuf,data_len - 6);

						if(crc16_read == crc16_cal)
						{
							iap_write_appbin(AppFlashAdd + 128 * bag_pos,DownLoadBuf,data_len - 6,1);
							bag_pos ++;

							crc32_read = (((u32)DownLoadBuf[data_len - 1]) << 24) \
											+ (((u32)DownLoadBuf[data_len - 2]) << 16) \
											+ (((u32)DownLoadBuf[data_len - 3]) << 8) \
											+ (((u32)DownLoadBuf[data_len - 4]));

							file_len = 128 * (NewFirmWareBagNum - 1) + (data_len - 6);

							k_num = file_len / 1024;
							last_k_byte_num = file_len % 1024;
							if(last_k_byte_num > 0)
							{
								k_num += 1;
							}

							for(i = 0; i < k_num; i ++)
							{
								memset(crc32_cal_buf,0,1024);
								if(i < k_num - 1)
								{
									STMFLASH_ReadBytes(AppFlashAdd + 1024 * i,crc32_cal_buf,1024);
									crc32_cal = CRC32(crc32_cal_buf,1024,crc32_cal,0);
								}
								if(i == k_num - 1)
								{
									if(last_k_byte_num == 0)
									{
										STMFLASH_ReadBytes(AppFlashAdd + 1024 * i,crc32_cal_buf,1024);
										crc32_cal = CRC32(crc32_cal_buf,1024,crc32_cal,1);
									}
									else if(last_k_byte_num > 0)
									{
										STMFLASH_ReadBytes(AppFlashAdd + 1024 * i,crc32_cal_buf,last_k_byte_num);
										crc32_cal = CRC32(crc32_cal_buf,last_k_byte_num,crc32_cal,1);
									}
								}
							}

							if(crc32_read == crc32_cal)
							{
								__disable_irq();		//�ر�ȫ���жϣ��������Ҷ�дEEPROM
								do
								{
									ResetOTAInfo(HoldReg);
									ReadOTAInfo(HoldReg);

									if(ReadOTAInfo(HoldReg))
									{
										__enable_irq();	//�ָ�ȫ���ж�
										return 0xAA;
									}

									store_times ++;

									if(store_times > 5)
									{
										__enable_irq();	//�ָ�ȫ���ж�
										return 0xAA;
									}
								}
								while(!ReadOTAInfo(HoldReg));
							}
							else		//����CRC32����
							{
								bag_pos --;
								ret = 3;
							}
						}
						else			//ĩ��CRC16����
						{
							ret = 2;
						}
					}
					else				//ĩ�����ݳ��ȴ���
					{
						ret = 4;
					}
				}
				else					//���ݰ���������
				{
					ret = 5;
				}
			}
			else						//û�����ݷ��ء������ǳ�ʱ����������ԭ��
			{
				ret = 0;
			}
		}
		else							//URL������
		{
			ret = 6;
		}
	}
	else								//���ݰ���������
	{
		ret = 5;
	}

	return ret;
}












