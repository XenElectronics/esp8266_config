/*
 * httpHandler.c
 *
 *  Created on: May 19, 2016
 *      Author: Danius
 */
#include "httpHandler.h"
#include <osapi.h>
#include <user_interface.h>
#include "c_types.h"
#include "user_config.h"

callbackParams localCallbackParams;

void handleGet(struct espconn *pEspConn, httpHeaderStruct *header) {
	if (strcmp(header->path, "/") == 0) {
		uint8 header[] = "HTTP/1.0 200 OK\r\n\r\n"; //"HTTP/1.0 200 OK\r\nContent-Encoding:gzip\r\n\r\n";
		sendData(pEspConn, header, sizeof(header), (uint8 *) HTML_POS,
		HTML_SIZE);
	} else if (strcmp(header->path, "/scanWifi")) {
		localCallbackParams.pEspConn = pEspConn;
		//localEspConn = pEspConn;
		wifi_station_scan(NULL, scanCB);
	} else {
		char data[] =
				"HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\n\r\n<HTML><HEAD><TITLE>ESP8266</TITLE><link rel='icon' href='data:;base64,iVBORw0KGgo='></HEAD><BODY><H1>Shit man this page is not here</H1><p>I'm just a small ESP8266 module and I have to handle this error page</BODY></HTML>\r\n";
		int length = sizeof(data);
		espconn_send(pEspConn, data, length);
		espconn_disconnect(pEspConn);
	}
}

void sendData(struct espconn *pEspConn, uint8 *header, uint32 headerLength,
		uint8 *dataAddrFlash, uint32 dataLength) {
	headerLength -= 1; //Don't take last char (null)
	uint8 buffer[CHUNK_SIZE];
	memcpy(buffer, header, headerLength);
	if ((headerLength + dataLength) > CHUNK_SIZE) {
		localCallbackParams.pEspConn = pEspConn;
		//localEspConn = pEspConn;
		localCallbackParams.amountOfSends = (headerLength + dataLength) / CHUNK_SIZE;
		localCallbackParams.sendRemainder = (headerLength + dataLength) % CHUNK_SIZE;
		if (localCallbackParams.sendRemainder > 0)
			localCallbackParams.amountOfSends++;
		readFlashUnaligned(buffer + headerLength, (char *) dataAddrFlash,
		CHUNK_SIZE - headerLength);
		localCallbackParams.flashDataAddr = dataAddrFlash + CHUNK_SIZE - headerLength;
		localCallbackParams.chunkedSend = true;
		localCallbackParams.amountOfSent = 0;
		int i;
		for (i = 0; i < CHUNK_SIZE; i++) {
			os_printf("%c", buffer[i]);
		}
		os_printf("\r\n");

		espconn_regist_sentcb(pEspConn, sentCB);
		espconn_send(pEspConn, buffer, (uint16) CHUNK_SIZE);
	} else {
		localCallbackParams.chunkedSend = false;
		readFlashUnaligned(buffer + headerLength, (char *) dataAddrFlash,
				dataLength);
		espconn_send(pEspConn, buffer, (uint16) (dataLength + headerLength));
		espconn_disconnect(pEspConn);
	}
}

void sendDataChunk(struct espconn *pEspConn, uint8 *dataAddrFlash,
		uint32 dataLength) {
	os_printf("Send data chunk data length: %d\r\n", dataLength);
	uint8 buffer[CHUNK_SIZE];
	readFlashUnaligned(buffer, (char *) dataAddrFlash, dataLength);
	localCallbackParams.flashDataAddr += dataLength;
	int i;
	for (i = 0; i < dataLength; i++) {
		os_printf("%c", buffer[i]);
	}
	os_printf("\r\n");
	espconn_send(pEspConn, buffer, (uint16) dataLength);
}

void readFlash(char *dst, char *src, int len) {
	if (len > CHUNK_SIZE) {
		int amountOfReads = len / CHUNK_SIZE;
		int remainder = len % CHUNK_SIZE;
		int i;

		for (i = 0; i < amountOfReads; i++) {
			readFlashUnaligned(dst, src, CHUNK_SIZE);
			dst += CHUNK_SIZE;
			src += CHUNK_SIZE;
		}

		if (remainder > 0) {
			readFlashUnaligned(dst, src, remainder);
		}
	} else {
		readFlashUnaligned(dst, src, len);
	}
}

void readFlashUnaligned(char *dst, char *src, int len) {
	uint8_t src_offset = ((uint32_t) src) & 3;
	uint32_t src_address = ((uint32_t) src) - src_offset;
	uint32_t tmp_buf[len / 4 + 2];

	os_printf("Read address: %d\r\n", src_address);

	SpiFlashOpResult readResult = spi_flash_read((uint32) src_address,
			(uint32*) tmp_buf, len + src_offset);

	switch (readResult) {
	case SPI_FLASH_RESULT_OK:
		memcpy(dst, ((uint8_t*) tmp_buf) + src_offset, len);
		break;
	case SPI_FLASH_RESULT_ERR:
		os_printf("SPI flash read error!\r\n");
		break;
	case SPI_FLASH_RESULT_TIMEOUT:
		os_printf("SPI flash read timeout\r\n");
		break;
	}
}

void sentCB(void *arg) {
	localCallbackParams.amountOfSent++;
	os_printf("Amount of sent: %d, amount of sends: %d\r\n", localCallbackParams.amountOfSent,
			localCallbackParams.amountOfSends);
	if (localCallbackParams.chunkedSend && (localCallbackParams.amountOfSent <= localCallbackParams.amountOfSends)) {
		if (localCallbackParams.amountOfSent < localCallbackParams.amountOfSends - 1) {
			sendDataChunk(localCallbackParams.pEspConn, localCallbackParams.flashDataAddr, CHUNK_SIZE);
		} else {
			sendDataChunk(localCallbackParams.pEspConn, localCallbackParams.flashDataAddr, localCallbackParams.sendRemainder);
			espconn_disconnect(localCallbackParams.pEspConn);
		}
	}
}

void scanCB(void *arg, STATUS status) {
	struct bss_info *bssInfo;
	bssInfo = (struct bss_info *) arg;
	// skip the first in the chain � it is invalid
	bssInfo = STAILQ_NEXT(bssInfo, next);

	char buffer[300] = "HTTP/1.0 200 OK\r\n\r\n{\"ssids\":[";
	uint16 i = strlen(buffer);
	uint16 headerLength = i;

	bool first = true;
	while (bssInfo != NULL ) {
		if (strstr(buffer + headerLength, bssInfo->ssid) == NULL) {
			os_printf("ssid: %s\n", bssInfo->ssid);
			if (first) {
				first = !first;
			}
			else {
				buffer[i] = ',';
				i++;
			}
			buffer[i] = '"';
			i++;
			memcpy(buffer + i, bssInfo->ssid, strlen(bssInfo->ssid));
			i += strlen(bssInfo->ssid);
			buffer[i] = '"';
			i++;
		}
		bssInfo = STAILQ_NEXT(bssInfo, next);
	}
	memcpy(buffer + i, "]}", 2);
	espconn_send(localCallbackParams.pEspConn, buffer, strlen(buffer));
	espconn_disconnect(localCallbackParams.pEspConn);
}
