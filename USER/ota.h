#ifndef __OTA_H
#define __OTA_H

#include "sys.h"
#include "bcxx.h"


typedef  void (*iapfun)(void);				//����һ���������͵Ĳ���.   



extern CONNECT_STATE_E ConnectState;

void BootLoader_ExitInit(void);								
void iap_load_app(u32 appxaddr);			//��ת��APP����ִ��
void iap_write_appbin(u32 appxaddr,u8 *appbuf,u32 appsize, u8 flag);	//��ָ����ַ��ʼ,д��bin
u8 NeedUpDateNewFirmWare(void);



u8 FirmWareUpDate(void);
u8 OnServerHandle(u8 socket);

u8 TryToConnectToServer(void);




























#endif
