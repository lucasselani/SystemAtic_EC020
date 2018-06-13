/******************************************************************
 *****                                                        *****
 *****  Name: easyweb.c                                       *****
 *****  Ver.: 1.0                                             *****
 *****  Date: 07/05/2001                                      *****
 *****  Auth: Andreas Dannenberg                              *****
 *****        HTWK Leipzig                                    *****
 *****        university of applied sciences                  *****
 *****        Germany                                         *****
 *****        adannenb@et.htwk-leipzig.de                     *****
 *****  Func: implements a dynamic HTTP-server by using       *****
 *****        the easyWEB-API                                 *****
 *****  Rem.: In IAR-C, use  linker option                    *****
 *****        "-e_medium_write=_formatted_write"              *****
 *****                                                        *****
 ******************************************************************/
#include "stdlib.h"
#include "stdio.h"
#include "string.h"

// CodeRed - added #define extern on next line (else variables
// not defined). This has been done due to include the .h files 
// rather than the .c files as in the original version of easyweb.
#define extern 
#include "easyweb.h"
//CodeRed - added for LPC ethernet controller
#include "ethmac.h"
// CodeRed - include .h rather than .c file
#include "tcpip.h"                               // easyWEB TCP/IP stack
// webside for our HTTP server (HTML)
#include "webside.h"
// CodeRed - added NXP LPC register definitions header
#include "LPC17xx.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_timer.h"

#include "light.h"
#include "oled.h"
#include "acc.h"
#include "joystick.h"
#include "rotary.h"

// CodeRed - added for use in dynamic side of web page
unsigned int aaPagecounter = 0;
unsigned int adcValue = 0;
static uint8_t buf[10];

static void intToString(int value, uint8_t* pBuf, uint32_t len, uint32_t base) {
	static const char* pAscii = "0123456789abcdefghijklmnopqrstuvwxyz";
	int pos = 0;
	int tmpValue = value;

	// the buffer must not be null and at least have a length of 2 to handle one
	// digit and null-terminator
	if (pBuf == NULL || len < 2) {
		return;
	}

	// a valid base cannot be less than 2 or larger than 36
	// a base value of 2 means binary representation. A value of 1 would mean only zeros
	// a base larger than 36 can only be used if a larger alphabet were used.
	if (base < 2 || base > 36) {
		return;
	}

	// negative value
	if (value < 0) {
		tmpValue = -tmpValue;
		value = -value;
		pBuf[pos++] = '-';
	}

	// calculate the required length of the buffer
	do {
		pos++;
		tmpValue /= base;
	} while (tmpValue > 0);

	if (pos > len) {
		// the len parameter is invalid.
		return;
	}

	pBuf[pos] = '\0';

	do {
		pBuf[--pos] = pAscii[value % base];
		value /= base;
	} while (value > 0);

	return;

}

static void init_ssp(void) {
	SSP_CFG_Type SSP_ConfigStruct;
	PINSEL_CFG_Type PinCfg;

	/*
	 * Initialize SPI pin connect
	 * P0.7 - SCK;
	 * P0.8 - MISO
	 * P0.9 - MOSI
	 * P2.2 - SSEL - used as GPIO
	 */
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 7;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 8;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 9;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Funcnum = 0;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 2;
	PINSEL_ConfigPin(&PinCfg);

	SSP_ConfigStructInit(&SSP_ConfigStruct);

	// Initialize SSP peripheral with parameter given in structure above
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

	// Enable SSP peripheral
	SSP_Cmd(LPC_SSP1, ENABLE);

}

static void init_i2c(void) {
	PINSEL_CFG_Type PinCfg;

	/* Initialize I2C2 pin connect */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 11;
	PINSEL_ConfigPin(&PinCfg);

	// Initialize I2C peripheral
	I2C_Init(LPC_I2C2, 100000);

	/* Enable I2C1 operation */
	I2C_Cmd(LPC_I2C2, ENABLE);
}

void drawDisplay(uint8_t* text, uint32_t value, oled_color_t backgroundColor, oled_color_t textColor, uint8_t line) {
	uint8_t xPos = 1 + (8 * (line - 1));
	uint8_t yPos = 8 * line;
	/* showing text */
	oled_putString(1, xPos, text, backgroundColor, textColor);
	/* output values to OLED display */
	if(value != 0) {
		intToString(value, buf, 10, 10);
		oled_fillRect((1 + 9 * 6), xPos, 80, yPos, textColor);
		oled_putString((1 + 9 * 6), xPos, buf, backgroundColor, textColor);
	}
}

void showWelcomeScreen(oled_color_t backgroundColor, oled_color_t textColor) {
	oled_clearScreen(backgroundColor);
	drawDisplay("SystemAtic", 0, backgroundColor, textColor, 1);
	drawDisplay("EC 020 - P9", 0, backgroundColor, textColor, 2);
}

int main(void) {
	uint32_t lux = 0;
    int32_t xoff = 0;
    int32_t yoff = 0;
    int32_t zoff = 0;

    int8_t x = 0;
    int8_t y = 0;
    int8_t z = 0;

	uint8_t joy = -1;
	uint8_t lastJoy = -1;
	uint8_t rotation = -1;
	uint8_t lastRotation = -1;

	oled_color_t backgroundColor = OLED_COLOR_BLACK;
	oled_color_t textColor = OLED_COLOR_WHITE;

	init_i2c();
	init_ssp();

	oled_init();
	joystick_init();
	rotary_init();

	acc_init();
	light_init();

    acc_read(&x, &y, &z);
    xoff = 0-x;
    yoff = 0-y;
    zoff = 64-z;

	light_enable();
	light_setRange(LIGHT_RANGE_4000);

	TCPLowLevelInit();
	HTTPStatus = 0; // clear HTTP-server's flag register
	TCPLocalPort = TCP_PORT_HTTP; // set port we want to listen to

	oled_clearScreen(backgroundColor);
	showWelcomeScreen(backgroundColor, textColor);
	while (1)
	{
		/* Joystick */
		joy = joystick_read();
		/* Rotary switch */
		rotation = rotary_read();

		if (rotation != lastRotation && rotation != ROTARY_WAIT) {
			oled_color_t temp = backgroundColor;
			backgroundColor = textColor;
			textColor = temp;
			lastRotation = rotation;
			oled_clearScreen(backgroundColor);
		}

		if ((joy & JOYSTICK_LEFT) != 0 || lastJoy == JOYSTICK_LEFT) {
	        /* Accelerometer */
	        acc_read(&x, &y, &z);
	        x = x+xoff;
	        y = y+yoff;
	        z = z+zoff;
	        /* clearing if changed sides */
	        if(lastJoy != JOYSTICK_LEFT) {
	        	oled_clearScreen(backgroundColor);
	        }
			/* Saving last value */
			lastJoy = JOYSTICK_LEFT;
			/* Showing values */
			drawDisplay("X Pos: ", x, backgroundColor, textColor, 1);
			drawDisplay("Y Pos: ", y, backgroundColor, textColor, 2);
			drawDisplay("Z Pos: ", z, backgroundColor, textColor, 3);
		}

		if ((joy & JOYSTICK_RIGHT) != 0 || lastJoy == JOYSTICK_RIGHT) {
			/* light */
			lux = light_read();
	        /* clearing if changed sides */
	        if(lastJoy != JOYSTICK_RIGHT) {
	        	oled_clearScreen(backgroundColor);
	        }
			/* Saving last value */
			lastJoy = JOYSTICK_RIGHT;
			/* Showing values */
			drawDisplay("Light: ", lux, backgroundColor, textColor, 1);
		}

		if (!(SocketStatus & SOCK_ACTIVE))
			TCPPassiveOpen(); // listen for incoming TCP-connection
		DoNetworkStuff(); // handle network and easyWEB-stack
		HTTPServer();
	}
}

// This function implements a very simple dynamic HTTP-server.
// It waits until connected, then sends a HTTP-header and the
// HTML-code stored in memory. Before sending, it replaces
// some special strings with dynamic values.
// NOTE: For strings crossing page boundaries, replacing will
// not work. In this case, simply add some extra lines
// (e.g. CR and LFs) to the HTML-code.
void HTTPServer(void) {
	if (SocketStatus & SOCK_CONNECTED) // check if somebody has connected to our TCP
	{
		if (SocketStatus & SOCK_DATA_AVAILABLE) // check if remote TCP sent data
			TCPReleaseRxBuffer();                      // and throw it away

		if (SocketStatus & SOCK_TX_BUF_RELEASED) // check if buffer is free for TX
		{
			if (!(HTTPStatus & HTTP_SEND_PAGE)) // init byte-counter and pointer to webside
			{                                          // if called the 1st time
				HTTPBytesToSend = sizeof(WebSide) - 1; // get HTML length, ignore trailing zero
				PWebSide = (unsigned char *) WebSide;    // pointer to HTML-code
			}

			if (HTTPBytesToSend > MAX_TCP_TX_DATA_SIZE) // transmit a segment of MAX_SIZE
			{
				if (!(HTTPStatus & HTTP_SEND_PAGE)) // 1st time, include HTTP-header
				{
					memcpy(TCP_TX_BUF, GetResponse, sizeof(GetResponse) - 1);
					memcpy(TCP_TX_BUF + sizeof(GetResponse) - 1, PWebSide,
							MAX_TCP_TX_DATA_SIZE - sizeof(GetResponse) + 1);
					HTTPBytesToSend -= MAX_TCP_TX_DATA_SIZE
							- sizeof(GetResponse) + 1;
					PWebSide += MAX_TCP_TX_DATA_SIZE - sizeof(GetResponse) + 1;
				} else {
					memcpy(TCP_TX_BUF, PWebSide, MAX_TCP_TX_DATA_SIZE);
					HTTPBytesToSend -= MAX_TCP_TX_DATA_SIZE;
					PWebSide += MAX_TCP_TX_DATA_SIZE;
				}

				TCPTxDataCount = MAX_TCP_TX_DATA_SIZE;   // bytes to xfer
				InsertDynamicValues();               // exchange some strings...
				TCPTransmitTxBuffer();                   // xfer buffer
			} else if (HTTPBytesToSend)               // transmit leftover bytes
			{
				memcpy(TCP_TX_BUF, PWebSide, HTTPBytesToSend);
				TCPTxDataCount = HTTPBytesToSend;        // bytes to xfer
				InsertDynamicValues();               // exchange some strings...
				TCPTransmitTxBuffer();                   // send last segment
				TCPClose();                              // and close connection
				HTTPBytesToSend = 0;                     // all data sent
			}

			HTTPStatus |= HTTP_SEND_PAGE;              // ok, 1st loop executed
		}
	} else
		HTTPStatus &= ~HTTP_SEND_PAGE;       // reset help-flag if not connected
}

// Code Red - GetAD7Val function replaced
// Rather than using the AD convertor, in this version we simply increment
// a counter the function is called, wrapping at 1024. 
volatile unsigned int aaScrollbar = 400;
unsigned int GetAD7Val(void) {
	aaScrollbar = (aaScrollbar + 16) % 1024;
	adcValue = (aaScrollbar / 10) * 1000 / 1024;
	return aaScrollbar;
}

// searches the TX-buffer for special strings and replaces them
// with dynamic values (AD-converter results)
// Code Red - new version of InsertDynamicValues()
void InsertDynamicValues(void) {
	unsigned char *Key;
	char NewKey[6];
	unsigned int i;

	if (TCPTxDataCount < 4)
		return;                     // there can't be any special string

	Key = TCP_TX_BUF;

	for (i = 0; i < (TCPTxDataCount - 3); i++) {
		if (*Key == 'A')
			if (*(Key + 1) == 'D')
				if (*(Key + 3) == '%')
					switch (*(Key + 2)) {
					case '8':                                 // "AD8%"?
					{
						sprintf(NewKey, "%04d", GetAD7Val()); // insert pseudo-ADconverter value
						memcpy(Key, NewKey, 4);
						break;
					}
					case '7':                                 // "AD7%"?
					{
						sprintf(NewKey, "%3u", adcValue); // copy saved value from previous read
						memcpy(Key, NewKey, 3);
						break;
					}
					case '1':                                 // "AD1%"?
					{
						sprintf(NewKey, "%4u", ++aaPagecounter); // increment and insert page counter
						memcpy(Key, NewKey, 4);
						break;
					}
				}
		Key++;
	}
}
