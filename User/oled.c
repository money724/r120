#include "oled.h"
#include "stdlib.h"
#include "oledfont.h"  	 
#include "delay.h"
#include "bmp.h"

#define u8 uint8_t
#define u32 uint32_t
//OLED���Դ�
//��Ÿ�ʽ����.
//[0]0 1 2 3 ... 127	
//[1]0 1 2 3 ... 127	
//[2]0 1 2 3 ... 127	
//[3]0 1 2 3 ... 127	
//[4]0 1 2 3 ... 127	
//[5]0 1 2 3 ... 127	
//[6]0 1 2 3 ... 127	
//[7]0 1 2 3 ... 127 			   
/**********************************************
//IIC Start
**********************************************/

/**********************************************
//IIC Start
**********************************************/
void IIC_Start()
{

	OLED_SCLK_Set() ;
	OLED_SDIN_Set();
	OLED_SDIN_Clr();
	OLED_SCLK_Clr();
}

/**********************************************
//IIC Stop
**********************************************/
void IIC_Stop()
{
OLED_SCLK_Set() ;
//	OLED_SCLK_Clr();
	OLED_SDIN_Clr();
	OLED_SDIN_Set();
	
}

void IIC_Wait_Ack()
{

	//GPIOB->CRH &= 0XFFF0FFFF;	//����PB12Ϊ��������ģʽ
	//GPIOB->CRH |= 0x00080000;
//	OLED_SDA = 1;
//	delay_us(1);
	//OLED_SCL = 1;
	//delay_us(50000);
/*	while(1)
	{
		if(!OLED_SDA)				//�ж��Ƿ���յ�OLED Ӧ���ź�
		{
			//GPIOB->CRH &= 0XFFF0FFFF;	//����PB12Ϊͨ���������ģʽ
			//GPIOB->CRH |= 0x00030000;
			return;
		}
	}
*/
	OLED_SCLK_Set() ;
	OLED_SCLK_Clr();
}
/**********************************************
// IIC Write byte
**********************************************/

void Write_IIC_Byte(unsigned char IIC_Byte)
{
	unsigned char i;
	unsigned char m,da;
	da=IIC_Byte;
	OLED_SCLK_Clr();
	for(i=0;i<8;i++)		
	{
			m=da;
		//	OLED_SCLK_Clr();
		m=m&0x80;
		if(m==0x80)
		{OLED_SDIN_Set();}
		else OLED_SDIN_Clr();
			da=da<<1;
		OLED_SCLK_Set();
		OLED_SCLK_Clr();
		}


}
/**********************************************
// IIC Write Command
**********************************************/
void Write_IIC_Command(unsigned char IIC_Command)
{
   IIC_Start();
   Write_IIC_Byte(0x78);            //Slave address,SA0=0
	 IIC_Wait_Ack();	
   Write_IIC_Byte(0x00);			//write command
	 IIC_Wait_Ack();	
   Write_IIC_Byte(IIC_Command); 
	 IIC_Wait_Ack();	
   IIC_Stop();
}
/**********************************************
// IIC Write Data
**********************************************/
void Write_IIC_Data(unsigned char IIC_Data)
{
   IIC_Start();
   Write_IIC_Byte(0x78);			//D/C#=0; R/W#=0
	 IIC_Wait_Ack();	
   Write_IIC_Byte(0x40);			//write data
	 IIC_Wait_Ack();	
   Write_IIC_Byte(IIC_Data);
	 IIC_Wait_Ack();	
   IIC_Stop();
}
void OLED_WR_Byte(unsigned dat,unsigned cmd)
{
	if(cmd)
			{

   Write_IIC_Data(dat);
   
		}
	else {
   Write_IIC_Command(dat);
		
	}


}


/********************************************
// fill_Picture
********************************************/
void fill_picture(unsigned char fill_Data)
{
	unsigned char m,n;
	for(m=0;m<8;m++)
	{
		OLED_WR_Byte(0xb0+m,0);		//page0-page1
		OLED_WR_Byte(0x00,0);		//low column start address
		OLED_WR_Byte(0x10,0);		//high column start address
		for(n=0;n<128;n++)
			{
				OLED_WR_Byte(fill_Data,1);
			}
	}
}


/***********************Delay****************************************/
void Delay_50ms(unsigned int Del_50ms)
{
	unsigned int m;
	for(;Del_50ms>0;Del_50ms--)
		for(m=6245;m>0;m--);
}

void Delay_1ms(unsigned int Del_1ms)
{
	unsigned char j;
	while(Del_1ms--)
	{	
		for(j=0;j<123;j++);
	}
}

//��������

	void OLED_Set_Pos(unsigned char x, unsigned char y) 
{ 	OLED_WR_Byte(0xb0+y,OLED_CMD);
	OLED_WR_Byte(((x&0xf0)>>4)|0x10,OLED_CMD);
	OLED_WR_Byte((x&0x0f),OLED_CMD); 
}   	  
//����OLED��ʾ    
void OLED_Display_On(void)
{
	OLED_WR_Byte(0X8D,OLED_CMD);  //SET DCDC����
	OLED_WR_Byte(0X14,OLED_CMD);  //DCDC ON
	OLED_WR_Byte(0XAF,OLED_CMD);  //DISPLAY ON
}
//�ر�OLED��ʾ     
void OLED_Display_Off(void)
{
	OLED_WR_Byte(0X8D,OLED_CMD);  //SET DCDC����
	OLED_WR_Byte(0X10,OLED_CMD);  //DCDC OFF
	OLED_WR_Byte(0XAE,OLED_CMD);  //DISPLAY OFF
}		   			 
//��������,������,������Ļ�Ǻ�ɫ��!��û����һ��!!!	  
void OLED_Clear(void)  
{  
	u8 i,n;		    
	for(i=0;i<8;i++)  
	{  
		OLED_WR_Byte (0xb0+i,OLED_CMD);    //����ҳ��ַ��0~7��
		OLED_WR_Byte (0x00,OLED_CMD);      //������ʾλ�á��е͵�ַ
		OLED_WR_Byte (0x10,OLED_CMD);      //������ʾλ�á��иߵ�ַ   
		for(n=0;n<128;n++)OLED_WR_Byte(0,OLED_DATA); 
	} //������ʾ
}

void OLED_PartClear(u8 x0,u8 y0)
{
	  u8 i,n;
    for( i=x0;i<y0;i++)
    {	
			OLED_WR_Byte (0xb0+i,OLED_CMD);    //����ҳ��ַ��0~7��
			OLED_WR_Byte (0x00,OLED_CMD);      //������ʾλ�á��е͵�ַ
			OLED_WR_Byte (0x10,OLED_CMD);      //������ʾλ�á��иߵ�ַ   			
			for(n=0;n<128;n++)
			{
				OLED_WR_Byte(0,OLED_DATA); 	
			}
    }    
}

void OLED_On(void)  
{  
	u8 i,n;		    
	for(i=0;i<8;i++)  
	{  
		OLED_WR_Byte (0xb0+i,OLED_CMD);    //����ҳ��ַ��0~7��
		OLED_WR_Byte (0x00,OLED_CMD);      //������ʾλ�á��е͵�ַ
		OLED_WR_Byte (0x10,OLED_CMD);      //������ʾλ�á��иߵ�ַ   
		for(n=0;n<128;n++)OLED_WR_Byte(1,OLED_DATA); 
	} //������ʾ
}
//��ָ��λ����ʾһ���ַ�,���������ַ�
//x:0~127
//y:0~63
//mode:0,������ʾ;1,������ʾ				 
//size:ѡ������ 16/12 
void OLED_ShowChar(u8 x,u8 y,u8 chr,u8 Char_Size)
{      	
	unsigned char c=0,i=0;	
		c=chr-' ';//�õ�ƫ�ƺ��ֵ			
		if(x>Max_Column-1){x=0;y=y+2;}
		if(Char_Size ==16)
			{
			OLED_Set_Pos(x,y);	
			for(i=0;i<8;i++)
			OLED_WR_Byte(F8X16[c*16+i],OLED_DATA);
			OLED_Set_Pos(x,y+1);
			for(i=0;i<8;i++)
			OLED_WR_Byte(F8X16[c*16+i+8],OLED_DATA);
			}
			else {	
				OLED_Set_Pos(x,y);
				for(i=0;i<6;i++)
				OLED_WR_Byte(F6x8[c][i],OLED_DATA);
				
			}
}
//m^n����
u32 oled_pow(u8 m,u8 n)
{
	u32 result=1;	 
	while(n--)result*=m;    
	return result;
}				  
//��ʾ2������
//x,y :�������	 
//len :���ֵ�λ��
//size:�����С
//mode:ģʽ	0,���ģʽ;1,����ģʽ
//num:��ֵ(0~4294967295);	 		  
void OLED_ShowNum(u8 x,u8 y,u32 num,u8 len,u8 size2)
{         	
	u8 t,temp;
	u8 enshow=0;						   
	for(t=0;t<len;t++)
	{
		temp=(num/oled_pow(10,len-t-1))%10;
		if(enshow==0&&t<(len-1))
		{
			if(temp==0)
			{
				OLED_ShowChar(x+(size2/2)*t,y,' ',size2);
				continue;
			}else enshow=1; 
		 	 
		}
	 	OLED_ShowChar(x+(size2/2)*t,y,temp+'0',size2); 
	}
} 
//��ʾһ���ַ��Ŵ�
void OLED_ShowString(u8 x,u8 y,u8 *chr,u8 Char_Size)
{
	unsigned char j=0;
	while (chr[j]!='\0')
	{		OLED_ShowChar(x,y,chr[j],Char_Size);
			x+=8;
		if(x>120){x=0;y+=2;}
			j++;
	}
}
//��ʾ����
void OLED_ShowCHinese(u8 x,u8 y,u8 no,u8 flag)
{      			    
	u8 t,adder=0;
	OLED_Set_Pos(x,y);	
	
	switch(flag)
	{
		case 0://�д���һ���о���
    for(t=0;t<16;t++) 
		{
			OLED_WR_Byte(Hzk[2*no][t],OLED_DATA);
			adder+=1;
    }	
		OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(Hzk[2*no+1][t],OLED_DATA);
			adder+=1;
    }	
		break;
		case 1://��ˮ����
		for(t=0;t<16;t++)
		{
			OLED_WR_Byte(floodswitchFont[2*no][t],OLED_DATA);
			adder+=1;
    }	
		  OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(floodswitchFont[2*no+1][t],OLED_DATA);
			adder+=1;
     }	
		break;
		 case 2://��ˮ��
		for(t=0;t<16;t++)
		{
			OLED_WR_Byte(floodswitchFont1[2*no][t],OLED_DATA);
			adder+=1;
    }	
		  OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(floodswitchFont1[2*no+1][t],OLED_DATA);
			adder+=1;
     }	
		break;
		 case 3://��ˮ��
		for(t=0;t<16;t++)
		{
			OLED_WR_Byte(floodswitchFont2[2*no][t],OLED_DATA);
			adder+=1;
    }	
		  OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(floodswitchFont2[2*no+1][t],OLED_DATA);
			adder+=1;
     }	
		break;
		case 4:  //Һλ�߶�
		for(t=0;t<16;t++)
		{
			OLED_WR_Byte(liquidFont[2*no][t],OLED_DATA);
			adder+=1;
    }	
		  OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(liquidFont[2*no+1][t],OLED_DATA);
			adder+=1;
    }	
		break;		
		case 5:  //״̬
		for(t=0;t<16;t++)
		{
			OLED_WR_Byte(compositeStsFont[2*no][t],OLED_DATA);
			adder+=1;
    }	
		  OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(compositeStsFont[2*no+1][t],OLED_DATA);
			adder+=1;
    }				
		break;    
		case 6:  //����
		for(t=0;t<16;t++)
		{
			OLED_WR_Byte(normalStsFont[2*no][t],OLED_DATA);
			adder+=1;
    }	
		  OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(normalStsFont[2*no+1][t],OLED_DATA);
			adder+=1;
    }				
		break;  
		case 7:  //ˮ����
		for(t=0;t<16;t++)
		{
			OLED_WR_Byte(liquidStsFont[2*no][t],OLED_DATA);
			adder+=1;
    }	
		  OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(liquidStsFont[2*no+1][t],OLED_DATA);
			adder+=1;
    }				
		break;  
		case 8:  //����
		for(t=0;t<16;t++)
		{
			OLED_WR_Byte(compositeAlarmFont[2*no][t],OLED_DATA);
			adder+=1;
    }	
		  OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(compositeAlarmFont[2*no+1][t],OLED_DATA);
			adder+=1;
    }				
		break;  
		case 9:  //����
		for(t=0;t<16;t++)
		{
			OLED_WR_Byte(faultFont[2*no][t],OLED_DATA);
			adder+=1;
    }	
		  OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(faultFont[2*no+1][t],OLED_DATA);
			adder+=1;
    }				
		break;  
		case 10:  //��ȼ����
		for(t=0;t<16;t++)
		{
			OLED_WR_Byte(gasFont[2*no][t],OLED_DATA);
			adder+=1;
    }	
		  OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(gasFont[2*no+1][t],OLED_DATA);
			adder+=1;
    }				
		break;  
		case 11:  //Ũ��
		for(t=0;t<16;t++)
		{
			OLED_WR_Byte(gasdensityFont[2*no][t],OLED_DATA);
			adder+=1;
    }	
		  OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(gasdensityFont[2*no+1][t],OLED_DATA);
			adder+=1;
    }				
		break; 
		case 12:  //�߶�
		for(t=0;t<16;t++)
		{
			OLED_WR_Byte(liquidhighFont[2*no][t],OLED_DATA);
			adder+=1;
    }	
		  OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
		{	
			OLED_WR_Byte(liquidhighFont[2*no+1][t],OLED_DATA);
			adder+=1;
    }				
		break;  		
		default:
		break;
		}			
}
/***********������������ʾ��ʾBMPͼƬ128��64��ʼ������(x,y),x�ķ�Χ0��127��yΪҳ�ķ�Χ0��7*****************/
void OLED_DrawBMP(unsigned char x0, unsigned char y0,unsigned char x1, unsigned char y1,unsigned char BMP[])
{ 	
 unsigned int j=0;
 unsigned char x,y;
  
  if(y1%8==0) y=y1/8;      
  else y=y1/8+1;
	for(y=y0;y<y1;y++)
	{
		OLED_Set_Pos(x0,y);
    for(x=x0;x<x1;x++)
	    {      
	    	OLED_WR_Byte(BMP[j++],OLED_DATA);	    	
	    }
	}
} 

//��ʼ��SSD1306					    
void OLED_Init(void)
{  
 	GPIO_InitTypeDef  GPIO_InitStructure; 	

	OLED_SCLK_GPIO_CLK_ENABLE();
	OLED_SDIN_GPIO_CLK_ENABLE();
	
	GPIO_InitStructure.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStructure.Pull  = GPIO_NOPULL;
	GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
	GPIO_InitStructure.Pin   = OLED_SCLK_PIN;
  HAL_GPIO_Init(OLED_SCLK_PORT, &GPIO_InitStructure);
	GPIO_InitStructure.Pin   = OLED_SDIN_PIN;
  HAL_GPIO_Init(OLED_SDIN_PORT, &GPIO_InitStructure);
	
	OLED_SCLK_Set();
	OLED_SDIN_Set();

	delay_ms(800);
	OLED_WR_Byte(0xAE,OLED_CMD);//--display off
	OLED_WR_Byte(0x00,OLED_CMD);//---set low column address
	OLED_WR_Byte(0x10,OLED_CMD);//---set high column address
	OLED_WR_Byte(0x40,OLED_CMD);//--set start line address  
	OLED_WR_Byte(0xB0,OLED_CMD);//--set page address
	OLED_WR_Byte(0x81,OLED_CMD); // contract control
	OLED_WR_Byte(0xFF,OLED_CMD);//--128   
	OLED_WR_Byte(0xA1,OLED_CMD);//set segment remap 
	OLED_WR_Byte(0xA6,OLED_CMD);//--normal / reverse
	OLED_WR_Byte(0xA8,OLED_CMD);//--set multiplex ratio(1 to 64)
	OLED_WR_Byte(0x3F,OLED_CMD);//--1/32 duty
	OLED_WR_Byte(0xC8,OLED_CMD);//Com scan direction
	OLED_WR_Byte(0xD3,OLED_CMD);//-set display offset
	OLED_WR_Byte(0x00,OLED_CMD);//
	
	OLED_WR_Byte(0xD5,OLED_CMD);//set osc division
	OLED_WR_Byte(0x80,OLED_CMD);//
	
	OLED_WR_Byte(0xD8,OLED_CMD);//set area color mode off
	OLED_WR_Byte(0x05,OLED_CMD);//
	
	OLED_WR_Byte(0xD9,OLED_CMD);//Set Pre-Charge Period
	OLED_WR_Byte(0xF1,OLED_CMD);//
	
	OLED_WR_Byte(0xDA,OLED_CMD);//set com pin configuartion
	OLED_WR_Byte(0x12,OLED_CMD);//
	
	OLED_WR_Byte(0xDB,OLED_CMD);//set Vcomh
	OLED_WR_Byte(0x30,OLED_CMD);//
	
	OLED_WR_Byte(0x8D,OLED_CMD);//set charge pump enable
	OLED_WR_Byte(0x14,OLED_CMD);//
	
	OLED_WR_Byte(0xAF,OLED_CMD);//--turn on oled panel
	
	OLED_Clear(); 
	OLED_DrawBMP(0,0,128,8,BMP1);	//��������	
	delay_ms(2000);
	OLED_Clear(); 
}  

/************************************************************
**����ԭ�ͣ�
**����˵����void
**����ֵ����
**˵����OLED��ʾ����
************************************************************/
void OLED_Update(void)
{   	
  char str[20];	    
	static u8 updateSts;	
	float liquidValueFloat;
	
	OLED_PartClear(4,6);	
	OLED_ShowCHinese(0,4,0,12);//��
	OLED_ShowCHinese(16,4,1,12);//��
	OLED_ShowString(32,4,":",16);//:		
	OLED_ShowString(112,4,"mm",16);//mm
	sprintf(str,"%d",liquidValueFloat);//�߶�ֵ							
	OLED_ShowString(48,4,str,16); 	 		
}
