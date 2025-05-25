/* ========================================
 *
 * Copyright YOUR COMPANY, THE YEAR
 * All Rights Reserved
 * UNPUBLISHED, LICENSED SOFTWARE.
 *
 * CONFIDENTIAL AND PROPRIETARY INFORMATION
 * WHICH IS THE PROPERTY OF your company.
 *
 * ========================================
*/
#include <project.h>
#include <string.h>

#include "can-controller/api.h"
#include "can-controller/device/mcp2515.h"

int32_t my_device_id = 100;

static void put_hex(uint8_t data)
{
    uint8_t elem = data >> 4;
    int i;
    SERIAL_UartPutChar(' ');
    for (i = 0; i < 2; ++i) {
        elem = (elem < 10) ? elem + '0' : elem + 'a' - 10;
        SERIAL_UartPutChar(elem);
        elem = data & 0x0f;
    }
}

uint8 led = 1;
CY_ISR(ISR_USER_SW)
{
    Pin_LED_B_Write(0);
    can_init();
    Pin_LED_B_Write(1);    
}

#define QUEUE_SIZE 128
volatile uint8_t q_first = 0;
volatile uint8_t q_last = 0;
can_message_t *queue_array[QUEUE_SIZE];

inline void queue_add(can_message_t *message)
{
    queue_array[q_last++] = message;
    if (q_last == QUEUE_SIZE)
        q_last = 0;
}

inline can_message_t *queue_remove()
{
    can_message_t *val = queue_array[q_first++];
    if (q_first == QUEUE_SIZE)
        q_first = 0;
    return val;
}

#define queue_empty() (q_first == q_last)

#define RXBnSIDL_SRR_BIT 4
#define RXBnSIDL_IDE_BIT 3

#define RXBnDLC_RFR_BIT 6

uint8_t pnt = 0;
char command[128];

void process_command(/*char command[], uint8_t len*/)
{
    if (strncasecmp(command, "reqPerfId", 4) == 0) {
        SERIAL_UartPutString("0x04\r\n");
    }
    else {
        SERIAL_UartPutString("Unknown command\r\n");
    }
}

int main()
{
    CyGlobalIntEnable; /* Enable global interrupts. */

    /* Place your initialization/startup code here (e.g. MyInst_Start()) */
    
    // SPIM_CAN_Start();
    SERIAL_Start();
    
    can_init();
    
    // for (;;) {}
    
    // dump all register values
    {
        SERIAL_UartPutString("Done configuring CAN controller:\r\n");

        int i;
        uint8_t buf[18];
        uint8_t addr;

        // test writing data to TxB0 data registers
        for (i = 0; i < 8; ++i)
            buf[7-i] = i + 1;
    
        for (addr = 0; addr < 0x80; addr += 0x10) {
            mcp2515_read(addr, buf, 16);
            // put_hex(addr);
            for (i = 0; i < 16; ++i)
                put_hex(buf[i+2]);
            SERIAL_UartPutString("\r\n");
        }
    }
    SERIAL_UartPutString("\r\nlistening...\r\n");    


    led = 0;
    Pin_LED_B_Write(led);

    // start listening
    isr_1_ClearPending();
    isr_1_StartEx(ISR_USER_SW);
    
    for(;;)
    {
        uint32 ch = SERIAL_UartGetChar();
        if (ch != 0) {
            if (ch == '\r') {
                SERIAL_UartPutString("\r\n");
                command[pnt] = 0;
                process_command(/*str, pnt*/);
                pnt = 0;
            }
            else {
                SERIAL_UartPutChar(ch);
                command[pnt++] = ch;
            }
        }
        if (!queue_empty()) {
            can_message_t *message = queue_remove();
            if (!message->is_extended) {
                SERIAL_UartPutString("std[");
                put_hex(message->id >> 8);
                put_hex(message->id);
            }
            else {
                SERIAL_UartPutString("ext[");
                put_hex(message->id >> 24);
                put_hex(message->id >> 16);
                put_hex(message->id >> 8);
                put_hex(message->id);
            }
            if (message->is_remote) {
                SERIAL_UartPutString(" ]: REMOTE\r\n");                
            }
            else {
                SERIAL_UartPutString(" ]:");                                
                int i;
                for (i = 0; i < message->data_length; ++i) {
                    put_hex(message->data[i]);
                }
                SERIAL_UartPutString("\r\n");                                
            }
            can_free_message(message);
        }
    }
}

void can_consume_rx_message(can_message_t *message)
{
    queue_add(message);
}

/* [] END OF FILE */
