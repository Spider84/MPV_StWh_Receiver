/*
 * main.c
 *
 *  Created on: 25 мая 2020 г.
 *      Author: spide
 */

#include <inttypes.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include "USI_UART_config.h"

#define RESET_TIMER 1000

volatile static uint16_t reset_timer = RESET_TIMER;

#define CRC7_Poly 0x89

static unsigned char MMC_CalcCRC7(unsigned char crc, unsigned char data)
{
	unsigned char treg = (crc | data);

	for (int BitCount = 0; BitCount <8; BitCount++)
	{
		if (treg & 0x80)
			treg ^= CRC7_Poly; // if carry out of CRC, then EOR with Poly (0x89)

		treg <<= 1; // Shift CRC-Data register left 1 bit
	}

	return treg;
}

ISR(TIMER1_OVF1_vect) {
	TCNT1 = 6;
	if (reset_timer)
		--reset_timer;
}

static void setOutputs(uint16_t outputs)
{
	//Ошибка круиза
	if (outputs & 0x2000) {
		outputs &= 0x1F0;
	}
	//Ошибка медиапульта
	if (outputs & 0x1000) {
		outputs &= 0x0F;
	}

	//Тут младший бит у меня - это кнопка включения круиза, она отдельным портом
	//Если кнопка нажата, то порт на вывод, а нога в землю.
	//Если не нажата, то порт на ввод, без подтяжки
	DDRA = ~((uint8_t)(outputs>>1));
	PORTA = 0;

	//Вот та самая кнопка включения круиза.
	PORTB = (PORTB & 0xBF) | ((outputs & 1)<<6);

//	if (PORTA & _BV(PA0))
//						PORTA &= ~_BV(PA0);
//					else
//						PORTA |= _BV(PA0);
}

int main (void)
{
	//Изначально всё выключено
	DDRA = 0x00;
	PORTA = 0x00;

	//Ждём 1сек на всякий случай.
	_delay_ms(1000);

	//WDT на 2 секнуды. Со сбросом по прихожду каждого байта начала пакета. Нет данных - ребуты.
	wdt_enable(WDTO_2S);

	USI_UART_Flush_Buffers();
	USI_UART_Initialise_Receiver();                                         // Initialisation for USI_UART receiver

	//после UART, там тоже работают с PORTB и мало ли чего....
	PORTB &= ~_BV(PB6);
	DDRB |= _BV(PB6);

	//таймер на 1мс, для системного Тика
	TCCR1A = 0;
	PLLCSR = 0;
	TCNT1 = 6;
	TCCR1B = _BV(CS12) | _BV(CS11);
	TIFR = _BV(TOV1);
	TIMSK |= _BV(TOIE1);

	set_sleep_mode(SLEEP_MODE_IDLE);
	sleep_enable();

	//Сброс WDT и началао начал
	wdt_reset();
	sei();                                                                  // Enable global interrupts

	uint8_t in_pkt = 0, len = 0, crc = 0;
	uint16_t tmp_out = 0;

	while(1) {
		while ( USI_UART_Data_In_Receive_Buffer() )
		{
			uint8_t c = USI_UART_Receive_Byte();

			wdt_reset();

			//Начало пакета
			if (c==0x80) {
				in_pkt = 1;
				len = 0;
				tmp_out = 0;
				crc = MMC_CalcCRC7(0x00, c);
			} else
			//Тело пакета
			//Только если 8ой бит = 0
			//И всего на 3 байта после начала
			if ((in_pkt) && ((c&0x80)==0) && (len<3)) {
				switch (len++) {
					case 0:
						tmp_out = (c<<7);
						crc = MMC_CalcCRC7(crc, c);
						break;
					case 1:
						tmp_out |= (c & 0x7F);
						crc = MMC_CalcCRC7(crc, c);
						break;
					case 2:
						//Дело дошло до CRC. Сходится?
						if ((crc>>1) == c) {
							setOutputs(tmp_out);

							//Обновляем таймер сброса, т.к. с нами кто-то говорит
							reset_timer = RESET_TIMER;
						}
					default:
						in_pkt = 0;
						break;
				}
			} else {
				//Такого быть не должно
				in_pkt = 0;
			}
		}

		//Если таймер сброса достиг нуля - нужно выключить всё.
		cli();
		uint16_t tmp = reset_timer;
		sei();
		if (!tmp)
			setOutputs(0);
		sleep_cpu();                                                          // Sleep when waiting for next event
	}
}
