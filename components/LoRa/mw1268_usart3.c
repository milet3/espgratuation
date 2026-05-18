#include "mw1268_usart3.h"
#include <string.h>

//串口接收缓存区 	 
uint8_t USART3_RX_BUF[USART3_MAX_RECV_LEN]; 	//接收缓冲
uint8_t USART3_TX_BUF[USART3_MAX_SEND_LEN]; 	//发送缓冲

//接收状态标记
//[15]:0,没有接收到数据;1,接收到了一批数据. 
//[14:0]:接收到的数据长度 
volatile uint16_t USART3_RX_STA = 0;   	 

/* -------------------------------------------------------
 *  USART3_IRQHandler
 *  使用 IDLE 中断判定数据包接收完成（替代 TIM7）
 * ------------------------------------------------------- */
void USART3_IRQHandler(void) 
{ 
    uint8_t res;
    volatile uint32_t dummy;

    if(USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    { 	  
        res = USART_ReceiveData(USART3); 	 	  
        if((USART3_RX_STA & 0x8000) == 0)           //一批数据未处理前不再接收新数据
        { 
            if((USART3_RX_STA & 0x7FFF) < USART3_MAX_RECV_LEN) 
            { 	 	 	 
                USART3_RX_BUF[USART3_RX_STA & 0x7FFF] = res;
                USART3_RX_STA++;
            }
            else 
            { 
                USART3_RX_STA |= 0x8000; 	 	 	//强制标记接收完成 
            } 
        } 
    } 

    if(USART_GetITStatus(USART3, USART_IT_IDLE) != RESET)
    {
        dummy = USART3->SR;                         //清除 IDLE 标志
        dummy = USART3->DR;
        (void)dummy;

        if((USART3_RX_STA & 0x7FFF) > 0)            //有数据则标记完成
        {
            USART3_RX_STA |= 0x8000;
        }
    }
}   

/* -------------------------------------------------------
 *  usart3_init
 *  初始化串口 3
 * ------------------------------------------------------- */
void usart3_init(uint32_t bound) 
{  
    NVIC_InitTypeDef NVIC_InitStructure; 
    GPIO_InitTypeDef GPIO_InitStructure; 
    USART_InitTypeDef USART_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); 
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE); 

    USART_DeInit(USART3);

    //USART3_TX PB10 
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10; 
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; 
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure); 

    //USART3_RX PB11 
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11; 
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &GPIO_InitStructure); 

    USART_InitStructure.USART_BaudRate = bound; 
    USART_InitStructure.USART_WordLength = USART_WordLength_8b; 
    USART_InitStructure.USART_StopBits = USART_StopBits_1; 
    USART_InitStructure.USART_Parity = USART_Parity_No; 
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; 
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; 

    USART_Init(USART3, &USART_InitStructure); 
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    USART_ITConfig(USART3, USART_IT_IDLE, ENABLE); //开启空闲中断检测
    USART_Cmd(USART3, ENABLE); 

    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn; 
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3; 
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; 
    NVIC_Init(&NVIC_InitStructure); 

    USART3_RX_STA = 0; 
} 

/* -------------------------------------------------------
 *  u3_printf
 *  串口 3 格式化输出
 * ------------------------------------------------------- */
void u3_printf(char* fmt,...)  
{  
    uint16_t i, j; 
    va_list ap; 
    va_start(ap, fmt); 
    vsprintf((char*)USART3_TX_BUF, fmt, ap);
    va_end(ap); 
    i = strlen((const char*)USART3_TX_BUF);
    for(j = 0; j < i; j++)
    { 
        while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
        USART_SendData(USART3, USART3_TX_BUF[j]); 
    } 
} 

/* -------------------------------------------------------
 *  usart3_rx
 *  串口接收控制
 * ------------------------------------------------------- */
void usart3_rx(uint8_t enable) 
{ 
    USART_InitTypeDef USART_InitStructure;
    USART_Cmd(USART3, DISABLE);

    USART_InitStructure.USART_BaudRate = USART3->BRR; // 保持当前波特率
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;

    if(enable) 
        USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    else 
        USART_InitStructure.USART_Mode = USART_Mode_Tx;

    USART_Init(USART3, &USART_InitStructure); 
    USART_Cmd(USART3, ENABLE); 
}

/* 波特率枚举索引 → 实际波特率（与 _LORA_BRDRATE 枚举一一对应）*/
static const uint32_t s_bps_table[] =
{
    1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200
};

/* -------------------------------------------------------
 *  usart3_bpsset
 *  功能: 将 USART3 切换到 LoRa 模块配置的通信参数
 *  参数: bps    — _LORA_BRDRATE 枚举值 (0~7)
 *        parity — _LORA_BRDVERIFT 枚举值 (0=8N1 / 1=8E1 / 2=8O1)
 * ------------------------------------------------------- */
void usart3_bpsset(uint8_t bps, uint8_t parity)
{
    USART_InitTypeDef USART_InitStructure;
    uint16_t word_len;
    uint16_t par;

    if (bps > 7) bps = 7;

    /* 校验位映射：有校验时帧格式变为 9-bit (8 数据 + 1 校验) */
    switch (parity)
    {
        case 1:  par = USART_Parity_Even; word_len = USART_WordLength_9b; break;
        case 2:  par = USART_Parity_Odd;  word_len = USART_WordLength_9b; break;
        default: par = USART_Parity_No;   word_len = USART_WordLength_8b; break;
    }

    USART_Cmd(USART3, DISABLE);

    USART_InitStructure.USART_BaudRate            = s_bps_table[bps];
    USART_InitStructure.USART_WordLength          = word_len;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = par;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);

    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);
    USART_Cmd(USART3, ENABLE);

    USART3_RX_STA = 0;
}
