/**************************************************************************//**
 * @file     main.c
 * @version  V1.00
 * $Revision: 6 $
 * $Date: 15/06/26 1:58p $
 * @brief    Demonstrate how to transmit LIN header and response.
 *
 * @note
 * Copyright (C) 2013 Nuvoton Technology Corp. All rights reserved.
*****************************************************************************/

#include <stdio.h>
#include "Nano100Series.h"
#include "uart.h"
#include "sys.h"
#include "clk.h"



#define RXBUFSIZE 3072

/*---LIN Check sum mode-------------------*/
#define MODE_CLASSIC    2
#define MODE_ENHANCED   1

/*---------------------------------------------------------------------------------------------------------*/
/* Global variables                                                                                        */
/*---------------------------------------------------------------------------------------------------------*/

/*---Using in UART Test -------------------*/
volatile uint8_t comRbuf[1024];
volatile uint16_t comRbytes = 0;        /* Available receiving bytes */
volatile uint16_t comRhead  = 0;
volatile uint16_t comRtail  = 0;
volatile int32_t g_bWait    = TRUE;
uint8_t u8SendData[12] = {0};

/*---Using in RS485 Test -------------------*/
uint8_t u8RecData[RXBUFSIZE]  = {0};
int32_t w_pointer =0;
volatile int32_t r_pointer = 0;
int32_t IsRS485ISR_TX_PORT = FALSE;
int32_t IsRS485ISR_RX_PORT = FALSE;

/*---Using in LIN Test -------------------*/
uint8_t testPattern[] = {0x00,0x55,0xAA,0xFF,0x00,0x55,0xFF,0xAA};

/*---------------------------------------------------------------------------------------------------------*/
/* Define functions prototype                                                                              */
/*---------------------------------------------------------------------------------------------------------*/
void UART_INT_HANDLE(uint32_t u32IntStatus);
int32_t DataCompare(uint8_t InBuffer[],uint8_t OutBuffer[],int32_t len);
extern char GetChar(void);

uint32_t GetUartClk(void)
{
    uint32_t clk =0, div;

    div = ( (CLK->CLKDIV0 & CLK_CLKDIV0_UART_N_Msk) >> 8) + 1;

    switch (CLK->CLKSEL1 & CLK_CLKSEL1_UART_S_Msk)
    {
    case 0:
        clk = __HXT; /* HXT */
        break;
    case 1:
        clk = __LXT;  /* LXT */
        break;
    case 2:
        clk = SysGet_PLLClockFreq(); /* PLL */
        break;
    case 3:
        clk = __HIRC12M; /* HIRC */
        break;
    }

    clk /= div;

    return clk;
}

/*---------------------------------------------------------------------------------------------------------*/
/* UART HANDLE                                                                                 */
/*---------------------------------------------------------------------------------------------------------*/
void LIN_HANDLE(void)
{
    int32_t i = 0;
    volatile uint32_t REG = 0;

    if(UART1->ISR & UART_ISR_LIN_IS_Msk)
    {
        UART1->TRSR |= UART_TRSR_LIN_RX_F_Msk;
        g_bWait = FALSE;
    }

    if(!g_bWait)
    {
        if(UART1->ISR & UART_ISR_RDA_IS_Msk)
        {
            u8RecData[r_pointer++] = UART1->RBR;
        }

        if(r_pointer==11)
        {
            printf("  %02x \t",u8RecData[1]);           /* ID */
            for(i =2; i<10; i++)
            {
                printf("%02x,",u8RecData[i] );          /* Data Bytes */
            }
            printf("  %02x \t",u8RecData[10] );         /* CheckSum */

            if(DataCompare(u8SendData,u8RecData,10))
            {
                printf("\tOK\n");
                r_pointer=0;
            }
            else
            {
                printf("...Failed\n");
            }
        }
    }
}

/*---------------------------------------------------------------------------------------------------------*/
/*  Sub-Function for LIN                                                                                   */
/*---------------------------------------------------------------------------------------------------------*/

/* Compute the checksum byte */
/* Offset :               */
/*    [1] : Compute not include ID  (LIN1.1) */
/*    [2] : Compute n include ID  (LIN2.0)   */

uint32_t cCheckSum(uint8_t DataBuffer[], uint32_t Offset)
{
    uint32_t i,CheckSum =0;

    for(i=Offset,CheckSum=0; i<=9; i++)
    {
        CheckSum+=DataBuffer[i];
        if (CheckSum>=256)
            CheckSum-=255;
    }
    return (255-CheckSum);
}

/* Compute the Parity Bit */
int8_t Parity(int i)
{
    int8_t number = 0 ;
    int8_t ID[6];
    int8_t p_Bit[2];
    int8_t mask =0;

    if(i>=64)
        printf("The ID is not match protocol\n");
    for(mask=0; mask<6; mask++)
        ID[mask] = (i & (1<<mask))>>mask;

    p_Bit[0] = (ID[0] + ID[1] + ID[2] + ID[4])%2;
    p_Bit[1] = (!((ID[1] + ID[3] + ID[4] + ID[5])%2));

    number = i + (p_Bit[0] <<6) + (p_Bit[1]<<7);
    return number;

}

int32_t DataCompare(uint8_t InBuffer[],uint8_t OutBuffer[],int32_t len)
{
    int i=0;
    for(i=0; i<len; i++)
    {
        if(InBuffer[i]!=OutBuffer[i])
        {
            printf("In[%d] = %x , Out[%d] = %d\n",i,InBuffer[i],i,OutBuffer[i]);
            return FALSE;
        }
    }
    return TRUE;
}

/*---------------------------------------------------------------------------------------------------------*/
/* Interrupt Handler                                                                                       */
/*---------------------------------------------------------------------------------------------------------*/
void UART1_IRQHandler(void)
{
    if((UART1->FUN_SEL & 0x3) == 0x1)   // LIN function
    {
        LIN_HANDLE();
    }
}

/*---------------------------------------------------------------------------------------------------------*/
/*  Only Send the LIN protocol header  (Sync.+ID + parity field)                                           */
/*---------------------------------------------------------------------------------------------------------*/

void LIN_HeaderSend(UART_T *tUART,int32_t id)
{
    w_pointer =0 ;

    /* Set LIN operation mode */
    tUART->ALT_CTL &= ~(UART_ALT_CTL_LIN_TX_BCNT_Msk | UART_ALT_CTL_LIN_RX_EN_Msk | UART_ALT_CTL_LIN_TX_EN_Msk);
    tUART->ALT_CTL |= (13 & UART_ALT_CTL_LIN_TX_BCNT_Msk);
    tUART->ALT_CTL |= (UART_ALT_CTL_LIN_TX_EN_Msk | UART_ALT_CTL_LIN_RX_EN_Msk);

    u8SendData[w_pointer++] = 0x55 ;                /* SYNC */

    u8SendData[w_pointer++] = Parity(id);           /* ID */

    UART_Write(tUART,u8SendData,2);
}

/*---------------------------------------------------------------------------------------------------------*/
/*  Only Send the LIN protocol response  (Data + CheckSum field)                                           */
/*---------------------------------------------------------------------------------------------------------*/

void LIN_ResponseSend(UART_T *tUART,int32_t checkSumOption,uint8_t Pattern[])
{
    int32_t i;

    for(i=0; i<8; i++)
        u8SendData[w_pointer++] = Pattern[i] ;

    u8SendData[w_pointer++] = cCheckSum(u8SendData,checkSumOption) ;

    UART_Write(tUART,u8SendData+2,9);
}

/*---------------------------------------------------------------------------------------------------------*/
/*  Sub-Function for LIN END                                                                               */
/*---------------------------------------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------------------------------------*/
/*  LIN Function Test                                                                                      */
/*---------------------------------------------------------------------------------------------------------*/
/*      Loopback Test by LIN mode                                                                          */
/*---------------------------------------------------------------------------------------------------------*/

void LIN_FunctionTest()
{
    int32_t i=0;

    printf("\n\n");
    printf("+-----------------------------------------------------------+\n");
    printf("|               LIN Function Test                           |\n");
    printf("+-----------------------------------------------------------+\n");
    printf("| The program is used to test LIN BUS.                      |\n");
    printf("| It will send ID 0~10 by a fixed pattern                   |\n");
    printf("| Enter any key to start                                    |\n");
    printf("+-----------------------------------------------------------+\n\n");
    getchar();

    printf("\nLIN Sample Demo. \n");

    /* Set UART Configuration */
    UART_Open(UART1, 9600);
    UART_SetLine_Config(UART1, 9600, UART_WORD_LEN_8, UART_PARITY_NONE, UART_STOP_BIT_1);
    UART_SelectLINMode(UART1,  (UART_ALT_CTL_LIN_RX_EN_Msk | UART_ALT_CTL_LIN_TX_EN_Msk), 5);

    UART_EnableInt(UART1, UART_IER_RLS_IE_Msk|UART_IER_RDA_IE_Msk|UART_IER_LIN_IE_Msk);
    NVIC_EnableIRQ(UART1_IRQn);

    printf("+-----------------------------------------------------------+\n");
    printf("|[ID]   [DATA]                   [CheckSum] [Result]        |\n");
    printf("+-----------------------------------------------------------+\n");

    for(i=0x00; i<10; i++)
    {
        g_bWait =TRUE;
        LIN_HeaderSend(UART1,i);
        while(g_bWait);
        LIN_ResponseSend(UART1,MODE_ENHANCED,testPattern);
        CLK_SysTickDelay(5000);
    }

    printf("\nLIN Sample Demo End.\n");

    UART_DisableInt(UART1, UART_IER_RDA_IE_Msk|UART_IER_LIN_IE_Msk);

    UART_Close(UART1);
}

void SYS_Init(void)
{
    /*---------------------------------------------------------------------------------------------------------*/
    /* Init System Clock                                                                                       */
    /*---------------------------------------------------------------------------------------------------------*/
    /* Unlock protected registers */
    SYS_UnlockReg();

    /* Enable External XTAL (4~24 MHz) */
    CLK->PWRCTL |= (0x1 << CLK_PWRCTL_HXT_EN_Pos); // HXT Enabled

    /* Waiting for 12MHz clock ready */
    CLK_WaitClockReady( CLK_CLKSTATUS_HXT_STB_Msk);

    /* Switch HCLK clock source to XTAL */
    CLK->CLKSEL0 &= ~CLK_CLKSEL0_HCLK_S_Msk;
    CLK->CLKSEL0 |= CLK_CLKSEL0_HCLK_S_HXT;

    /* Enable IP clock */
    CLK->APBCLK |= CLK_APBCLK_UART0_EN; // UART0 Clock Enable
    CLK->APBCLK |= CLK_APBCLK_UART1_EN; // UART1 Clock Enable

    /* Select IP clock source */
    CLK->CLKSEL1 &= ~CLK_CLKSEL1_UART_S_Msk;
    CLK->CLKSEL1 |= (0x0 << CLK_CLKSEL1_UART_S_Pos);// Clock source from external 12 MHz or 32 KHz crystal clock

    /* Update System Core Clock */
    /* User can use SystemCoreClockUpdate() to calculate PllClock, SystemCoreClock and CycylesPerUs automatically. */
    SystemCoreClockUpdate();

    /*---------------------------------------------------------------------------------------------------------*/
    /* Init I/O Multi-function                                                                                 */
    /*---------------------------------------------------------------------------------------------------------*/
    /* Set PB multi-function pins for UART0 RXD and TXD  */
    SYS->PB_L_MFP &= ~(SYS_PB_L_MFP_PB0_MFP_Msk | SYS_PB_L_MFP_PB1_MFP_Msk);
    SYS->PB_L_MFP |= (SYS_PB_L_MFP_PB0_MFP_UART0_RX | SYS_PB_L_MFP_PB1_MFP_UART0_TX);

    /* Set PB multi-function pins for UART1 RXD, TXD, RTS, CTS  */
    SYS->PB_L_MFP &= ~(SYS_PB_L_MFP_PB4_MFP_Msk | SYS_PB_L_MFP_PB5_MFP_Msk |
                       SYS_PB_L_MFP_PB6_MFP_Msk | SYS_PB_L_MFP_PB7_MFP_Msk);
    SYS->PB_L_MFP |= (SYS_PB_L_MFP_PB4_MFP_UART1_RX | SYS_PB_L_MFP_PB5_MFP_UART1_TX |
                      SYS_PB_L_MFP_PB6_MFP_UART1_RTS  | SYS_PB_L_MFP_PB7_MFP_UART1_CTS);

    /* Lock protected registers */
    SYS_LockReg();

}

void UART0_Init()
{
    /*---------------------------------------------------------------------------------------------------------*/
    /* Init UART                                                                                               */
    /*---------------------------------------------------------------------------------------------------------*/
    UART_Open(UART0, 115200);
}

void UART1_Init()
{
    /*---------------------------------------------------------------------------------------------------------*/
    /* Init UART                                                                                               */
    /*---------------------------------------------------------------------------------------------------------*/
    UART_Open(UART1, 57600);
}

/*---------------------------------------------------------------------------------------------------------*/
/* Main Function                                                                                           */
/*---------------------------------------------------------------------------------------------------------*/

int32_t main()
{
    /* Init System, IP clock and multi-function I/O */
    SYS_Init();
    /* Init UART0 for printf */
    UART0_Init();
    /* Init UART1 */
    UART1_Init();

    printf("\n\n");
    printf("+-----------------------------------------------------------+\n");
    printf("|               UART Sample Program                         |\n");
    printf("+-----------------------------------------------------------+\n");
    printf("| LIN function test                                         |\n");
    printf("+-----------------------------------------------------------+\n");

    LIN_FunctionTest();     /* LIN Function Test */

    while(1);
}

/*** (C) COPYRIGHT 2013 Nuvoton Technology Corp. ***/



