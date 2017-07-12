/* main.c : CAN
 * Tue May  9 20:58:59 2017	Warren W. Gay VE3WWG
 * Uses CAN on PB8/PB9, UART1 115200,"8N1","rw",1,1
 */
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "mcuio.h"
#include "miniprintf.h"
#include "canmsgs.h"
#include "monitor.h"

#define FLASH_MS		400		// Signal flash time in ms
#define GPIO_PORT_LED		GPIOC		// Builtin LED port
#define GPIO_LED		GPIO13		// Builtin LED

static SemaphoreHandle_t mutex;			// Handle to mutex
struct s_lamp_status lamp_status = { 0, 0, 0, 0, 0, 0 };

/*********************************************************************
 * Set PC13 LED On/Off
 *********************************************************************/
static void
led(bool on) {
	if ( on )
		gpio_clear(GPIO_PORT_LED,GPIO_LED);
	else	gpio_set(GPIO_PORT_LED,GPIO_LED);
}

/*********************************************************************
 * CAN Receive Callback
 *********************************************************************/
void
can_recv(struct s_canmsg *msg) {

	std_printf("[%4u(%d/%u):%c,$%02X]\n",
		(unsigned)msg->msgid,
		msg->fifo,(unsigned)msg->fmi,
		msg->rtrf ? 'R' : 'D',
		msg->data[0]);
}

/*********************************************************************
 * Signal Flash task
 *********************************************************************/

static void
flash_task(void *arg __attribute__((unused))) {
	struct s_lamp_en msg;

	for (;;) {
		xSemaphoreTake(mutex,portMAX_DELAY);		// Wait for mutex to be released
		xSemaphoreGive(mutex);				// Release mutex

		msg.enable = true;				// Not used for flash
		msg.reserved = 0;
		can_xmit(ID_Flash,false,false,sizeof msg,&msg);
		
		vTaskDelay(pdMS_TO_TICKS(FLASH_MS));
	}
}

/*********************************************************************
 * Enable/Disable a signal lamp or parking lamps
 *********************************************************************/

static void
lamp_enable(enum MsgID id,bool enable) {
	struct s_lamp_en msg;

	msg.enable = enable;
	msg.reserved = 0;
	can_xmit(id,false,false,sizeof msg,&msg);
}

/*********************************************************************
 * Console:
 *********************************************************************/
static void
console_task(void *arg __attribute__((unused))) {
	static bool lockedf, flashf = false;
	struct s_lamp_en msg;
	char ch;

	xSemaphoreTake(mutex,portMAX_DELAY);		// Initialize this as locked
	lockedf = true;

	std_printf("CAN Console Ready:\n");

        for (;;) {
		std_printf("> ");
		ch = std_getc();
		std_printf("%c\n",ch);

		switch ( ch ) {
		case 'F':
		case 'f':
			msg.enable = false; // Not used here
			msg.reserved = 0;
			can_xmit(ID_Flash,false,false,sizeof msg,&msg);
			break;
		case 'L':
		case 'l':
			lamp_status.left = ch == 'L';
			lamp_enable(ID_LeftEn,lamp_status.left);
			flashf = true;
			break;
		case 'R':
		case 'r':
			lamp_status.right = ch == 'R';
			lamp_enable(ID_RightEn,lamp_status.right);
			flashf = true;
			break;
		case 'P':
		case 'p':
			lamp_status.park = ch == 'P';
			lamp_enable(ID_ParkEn,lamp_status.park);
			break;
		case 'b':
		case 'B':
			lamp_status.brake = ch == 'B';
			lamp_enable(ID_BrakeEn,lamp_status.brake);
			break;
		default:
			std_printf("L/R/P??\n");
		}

		if ( flashf ) {
			if ( (lamp_status.left || lamp_status.right) && lockedf ) {
				xSemaphoreGive(mutex);				// Start flasher
				lockedf = false;
			} else if ( !lockedf && !lamp_status.left && !lamp_status.right ) {
				xSemaphoreTake(mutex,portMAX_DELAY);		// Stop flasher
				lockedf = true;
			}
			flashf = false;
		}
        }
}

/*
 * Main program: Device initialization etc.
 */
int
main(void) {

        rcc_clock_setup_in_hse_8mhz_out_72mhz();        // Use this for "blue pill"

        rcc_periph_clock_enable(RCC_GPIOC);
        gpio_set_mode(GPIO_PORT_LED,GPIO_MODE_OUTPUT_50_MHZ,GPIO_CNF_OUTPUT_PUSHPULL,GPIO_LED);

	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_set_mode(GPIOA,GPIO_MODE_OUTPUT_50_MHZ,GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,GPIO_USART1_TX);
	gpio_set_mode(GPIOA,GPIO_MODE_OUTPUT_50_MHZ,GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,GPIO11);

	std_set_device(mcu_uart1);			// Use UART1 for std I/O
        open_uart(1,115200,"8N1","rw",1,1);

	initialize_can(false,true,true);		// !nart, locked, altcfg=true PB8/PB9

	led(false);
	xTaskCreate(console_task,"console",200,NULL,configMAX_PRIORITIES-1,NULL);

	mutex = xSemaphoreCreateMutex();
	xTaskCreate(flash_task,"flash",100,NULL,configMAX_PRIORITIES-1,NULL);

	vTaskStartScheduler();
	for (;;);
}

// End main.c