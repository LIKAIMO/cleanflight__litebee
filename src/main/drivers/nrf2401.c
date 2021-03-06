#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <platform.h>
#include "nrf2401.h"
#include "stdio.h"
#include "bus_spi.h"
#include "bus_i2c.h"
#include "system.h"
#include "gpio.h"
#include "pwm_output.h"


int16_t roll1,pitch1,yaw1;
uint8_t  TXData[TX_PLOAD_WIDTH];//tx_data
uint8_t  TX_ADDRESS[TX_ADR_WIDTH]= {0x12,0xff,0xff,0xff,0xff};//tx_address

uint8_t  NRF24L01_RXDATA[RX_PLOAD_WIDTH];//rx_data
uint8_t  RX_ADDRESS[RX_ADR_WIDTH]= {0x11,0xff,0xff,0xff,0xff};//rx_address



void send_328p_buf(uint8_t len, uint8_t *buf)
{
	while(len)
	{
		i2cWrite(0x08,0,*buf);
		delayMicroseconds(8);
		buf++;
		len--;
	}
}

bool NRF_Write_Reg(uint8_t reg, uint8_t data)
{
    SPI_CSN_L();
    spiTransferByte(SPI2, reg + 0x20);
    spiTransferByte(SPI2, data);
    SPI_CSN_H();
    return true;
}

bool NRF_Write_Buf(uint8_t reg, uint8_t *data, uint8_t length)
{
    SPI_CSN_L();
    spiTransferByte(SPI2, reg + 0x20);
    spiTransfer(SPI2, NULL, data, length);
    SPI_CSN_H();
    return true;
}

bool NRF_Read_Buf(uint8_t reg, uint8_t *data, uint8_t length)
{
    SPI_CSN_L();
    spiTransferByte(SPI2, reg); // read transaction
    spiTransfer(SPI2, data, NULL, length);
    SPI_CSN_H();
    return true;
}


/****************NRF24L01_Receive*********************/
dataPackage mspData;

bool nrf_rx(void)
{
    uint8_t sta;
    static uint8_t count;
    NRF_Read_Buf(NRFRegSTATUS, &sta, 1);
    if(sta & (1<<RX_DR))
    {
        NRF_Read_Buf(RD_RX_PLOAD,NRF24L01_RXDATA,RX_PLOAD_WIDTH);// read receive payload from RX_FIFO buffer 
	memcpy(&mspData,NRF24L01_RXDATA,sizeof(mspData));
	NRF_Write_Reg(NRFRegSTATUS, sta);//清除nrf的中断标志位
	count = 0;
		
     }else count++;

	if(count > 25) 
	{	
		count = 0;
		return false;
	}else return true;
}


void rx_data_process(int16_t *buf)
{
	if(!strcmp("$M<",(char *)mspData.checkCode))
	{	
		if(mspData.led & 1 << 0) LED_A_ON;else LED_A_OFF;
		if(mspData.led & 1 << 1) LED_B_ON;else LED_B_OFF;
		if(mspData.led & 1 << 2) LED_C_ON;else LED_C_OFF;
		if(mspData.led & 1 << 3) LED_D_ON;else LED_D_OFF;
		
		if(mspData.mspCmd & ARM)	mwArm();
			else{	mwDisarm();
				buf[0] = 1500;buf[1] = 1500;buf[2] = 1500;buf[3] = 1100;
				mspData.dirdata = 0;}

		if(mspData.mspCmd & CALIBRATION)	accSetCalibrationCycles(400);
		
		if(mspData.mspCmd & ALTHOLD)	buf[4] = 1900;
			else buf[4] = 1100;
		
		if(mspData.mspCmd & ONLINE)	
		{	//LED_C_ON;
			if(mspData.dir)
			{	
				switch(mspData.dir)
				{
					case 	   UP: 
					case     DOWN: buf[3] = mspData.dirdata*4 + 1100;break;
					case  	 LEFT: buf[0] = 1500 - mspData.dirdata;break;
					case 	RIGHT: buf[0] = 1500 + mspData.dirdata;break;
					case  FORWARD: buf[1] = 1500 + mspData.dirdata;break;
					case BACKWARD: buf[1] = 1500 - mspData.dirdata;break;
					case   	   CR: buf[2] = 1500 + mspData.dirdata;break;
					case      CCR: buf[2] = 1500 - mspData.dirdata;break;
					default      : break;
				}
			}
		}
		else	
		{	
			//LED_C_OFF;
			buf[0] = mspData.roll;
			buf[1] = mspData.pitch;
			buf[2] = mspData.yaw;
			buf[3] = mspData.throttle; 
		}

		for(uint8_t i = 0;i<5;i++)	buf[i] = bound(buf[i],2000,1000);	
	}
}


/****************NRF24L01_TX*********************/
void nrf_tx(void)
{
	bool flag = true;
	uint8_t sta;

	TXData[0] = roll1;
	TXData[1] = roll1 >> 8;
	TXData[2] = pitch1;
	TXData[3] = pitch1 >> 8;
	TXData[4] = yaw1;
	TXData[5] = yaw1 >> 8;

	SPI_CE_L();
        NRF_Write_Buf(WR_TX_PLOAD - 0x20,TXData,TX_PLOAD_WIDTH);//写数据到TX BUF  32个字节
 	SPI_CE_H();//启动发送
	roll1 = 0;
	while(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0) && flag)
	{
		delayMicroseconds(10);
		roll1++;
		if(roll1 > 500)	
			flag = false;
	}
	NRF_Read_Buf(NRFRegSTATUS,&sta,1); //读取状态寄存器的值	   
	NRF_Write_Reg(NRFRegSTATUS,sta); //清除TX_DS或MAX_RT中断标志
	if(sta & MAX_TX)//达到最大重发次数
	NRF_Write_Reg(FLUSH_TX - 0X20,0xff);//清除TX FIFO寄存器
}


/************NFR24L01_Init************/
bool NRF24L01_INIT(void)
{
	uint8_t sta;
	nrf24l01HardwareInit();
	if(NRF24L01_Check())
	{
		SetRX_Mode();//default:0x11 ...
		for(uint8_t i = 0;i<5;i++)
		{
			NRF_Read_Buf(NRFRegSTATUS, &sta, 1);
			delay(10);
		}
		if(sta & (1<<RX_DR))
		{
	        	NRF_Read_Buf(RD_RX_PLOAD,NRF24L01_RXDATA,RX_PLOAD_WIDTH);// read receive payload from RX_FIFO buffer 
			memcpy(&mspData,NRF24L01_RXDATA,sizeof(mspData));
			NRF_Write_Reg(NRFRegSTATUS, sta);//清除nrf的中断标志位
			if(mspData.mspCmd & NEWADDRESS)
			{
				RX_ADDRESS[0] = mspData.motor[0];
				RX_ADDRESS[1] = mspData.motor[0] >> 8;
				RX_ADDRESS[2] = mspData.motor[1];
				RX_ADDRESS[3] = mspData.motor[1] >> 8;
				//save new_address to flash
				FLASH_Unlock();
				FLASH_ErasePage(0x0803E800);
				FLASH_ProgramWord(0x0803E800, mspData.motor[0]);
				FLASH_ProgramWord(0x0803E820, mspData.motor[1]);
				FLASH_Lock();
				SetRX_Mode();//use the new_address!
				for(uint8_t i = 0; i<5;i++)
				{
					TX_ADDRESS[i] = RX_ADDRESS[i];
					LED_C_ON;delay(50);
					LED_C_OFF;delay(50);
				}
			}
		}
		else
		{	//load the address form flash!
			RX_ADDRESS[0] = *(uint16_t *)0x0803E800;
			RX_ADDRESS[1] = *(uint16_t *)0x0803E800 >> 8;
			RX_ADDRESS[2] = *(uint16_t *)0x0803E820;
			RX_ADDRESS[3] = *(uint16_t *)0x0803E820 >> 8;
			SetRX_Mode();//use the new_address!
			for(uint8_t i = 0; i<5;i++)
			{
				TX_ADDRESS[i] = RX_ADDRESS[i];
				LED_D_ON;delay(50);
				LED_D_OFF;delay(50);
			}
		}
		return true;
	}
	else return false;
}


bool NRF24L01_Check(void) 
{ 
	uint8_t buf = 0x77; 
   	uint8_t buf1; 
	
	NRF_Write_Buf(TX_ADDR,&buf,1); 
	delay(2);
	NRF_Read_Buf(TX_ADDR,&buf1,1); 

	if(buf1 == 0x77)
		return true;
	else	return false;
} 

void SetRX_Mode(void)
{
    	SPI_CE_L();
	NRF_Write_Reg(FLUSH_RX,0xff);//清除TX FIFO寄存器			 
  	NRF_Write_Buf(RX_ADDR_P0,(uint8_t*)RX_ADDRESS,RX_ADR_WIDTH);//写RX节点地址
   	NRF_Write_Reg(EN_AA,0x01);       //使能通道0的自动应答    
  	NRF_Write_Reg(EN_RXADDR,0x01);   //使能通道0的接收地址  	 
  	NRF_Write_Reg(RF_CH,40);	 //设置RF通信频率		  
  	NRF_Write_Reg(RX_PW_P0,RX_PLOAD_WIDTH);//选择通道0的有效数据宽度 	    
  	NRF_Write_Reg(RF_SETUP,0x0f);   //设置TX发射参数,0db增益,2Mbps,低噪声增益开启   
  	NRF_Write_Reg(CONFIG, 0x0f);    //配置基本工作模式的参数;PWR_UP,EN_CRC,16BIT_CRC,接收模式 
    	SPI_CE_H();

} 

//发送模式
void SetTX_Mode(void)
{
    	SPI_CE_L();
    	NRF_Write_Reg(FLUSH_TX,0xff);//清除TX FIFO寄存器		  
    	NRF_Write_Buf(TX_ADDR,(uint8_t*)TX_ADDRESS,TX_ADR_WIDTH);		//写TX节点地址 
  	NRF_Write_Buf(RX_ADDR_P0,(uint8_t*)RX_ADDRESS,RX_ADR_WIDTH); 	//设置TX节点地址,主要为了使能ACK	  
  	NRF_Write_Reg(EN_AA,0x01);     //使能通道0的自动应答    
  	NRF_Write_Reg(EN_RXADDR,0x01); //使能通道0的接收地址  
  	NRF_Write_Reg(SETUP_RETR,0x1a);//设置自动重发间隔时间:500us + 86us;最大自动重发次数:10次
  	NRF_Write_Reg(RF_CH,40);       //设置RF通道为40
  	NRF_Write_Reg(RF_SETUP,0x0f);  //设置TX发射参数,0db增益,2Mbps,低噪声增益开启   
  	NRF_Write_Reg(CONFIG,0x0e);    //配置基本工作模式的参数;PWR_UP,EN_CRC,16BIT_CRC,接收模式,开启所有中断
    	SPI_CE_H();
  
} 

void nrf24l01HardwareInit(void)
{
	gpio_config_t IRQPIN;	//nrf24l01 pins

	IRQPIN.pin = Pin_0;
	IRQPIN.mode = Mode_IN_FLOATING;
	
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
	gpioInit(GPIOB,&IRQPIN);


	gpio_config_t CE;	//nrf24l01 pins

	CE.pin = Pin_1;
	CE.mode = Mode_Out_PP;
	CE.speed = Speed_10MHz;
	
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
	gpioInit(GPIOB,&CE);

	gpio_config_t LED;	//init led pins

	LED.pin = Pin_3 | Pin_4 | Pin_5 | Pin_2;
	LED.mode = Mode_Out_PP;
	LED.speed = Speed_2MHz;
	
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
	gpioInit(GPIOB,&LED);

}

