/*     
       A generic SmartCard firmware allowing for communication based 
       on ISO 7816 Part 3/Part 4 protocol standards, incorporating 
       safe AES-128 with masking and shuffling for decryption purposes.

       Authors:: Ermin Sakic, Thomas Wohlfahrt
 
       Licensed to the Apache Software Foundation (ASF) under one
       or more contributor license agreements.  See the NOTICE file
       distributed with this work for additional information
       regarding copyright ownership.  The ASF licenses this file
       to you under the Apache License, Version 2.0 (the
       "License"); you may not use this file except in compliance
       with the License.  You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

       Unless required by applicable law or agreed to in writing,
       software distributed under the License is distributed on an
       "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
       KIND, either express or implied.  See the License for the
       specific language governing permissions and limitations
       under the License.
*/

// Muss vor delay.h definiert sein, sonst falsche Wartezeiten
#define F_CPU 3270000UL
#include <avr/io.h>
#include <util/delay.h>
#include <inttypes.h>
#include <avr/interrupt.h>

#include "inv_aes.h"

//------------------------------------------------------------------------------
//	Define Settings:
//------------------------------------------------------------------------------
#define baudrate 9600UL

//------------------------------------------------------------------------------
//	PIN Defines:
//------------------------------------------------------------------------------
#define	LED7		PA7
#define	LED6		PA6
#define	LED5		PA5
#define	LED4		PA4
#define	LED3		PA3
#define	LED2		PA2
#define	LED1		PA1
#define	LED0		PA0
#define SCK			PB7
#define PDI_DATA	PB6 // I/O Leitung
#define MOSI		PB5
#define TRIGGER		PB4
#define SC_RESET	PB3
#define DEBUG_IO	PB2
#define SC_CLK		PB1
#define	BUTTON		PB0

//------------------------------------------------------------------------------
//	Variables:
//------------------------------------------------------------------------------
volatile uint8_t bitToBeSent;		//wird an der SC_IO zum Schreiben gestellt
volatile uint8_t receivedBit;		//wird an der SC_IO zum Lesen gestellt
volatile uint8_t transmitLock;
volatile uint8_t receiveLock;
volatile uint8_t startBitFlag;
uint8_t readyToReceiveByte;
uint8_t keyBuffer[16];
uint8_t state[16];
//uint8_t key[16] = { 0x55, 0xC1, 0x79, 0x04, 0xC3, 0xDC, 0x04, 0x52, 0x2A, 0x0C, 0x76, 0xEF, 0xE8, 0xCA, 0x48, 0xB5 };

//APDU command Variables
volatile uint8_t APDUcommandBuffer[5];
volatile uint8_t CLA;
volatile uint8_t INS;
volatile uint8_t PAR1;
volatile uint8_t PAR2;
volatile uint8_t PAR3;

//------------------------------------------------------------------------------
//	Sequences:
//------------------------------------------------------------------------------
/*
Clock rate conversion factor F
-------

----------------------------------------------------------------------
FI     |     0000      0001  0010  0011  0100  0101  0110  0111
--------------+-------------------------------------------------------
F      | Internal clk   372   558   744  1116  1488  1860  RFU
--------------+-------------------------------------------------------
fs (max) MHz |      -           5     6     8    12    16    20   -
----------------------------------------------------------------------

[T0] = 3B for direct conversion (Z-A <=> high/low state correspondence), [TS] = 0x90 bit map um TA1 und TD1 zu enablen,
Initial ETU = 372/f; work etu = (1/D)*(F/f); <=> [TA1] = 0x11 for FI = 1 (F=372) and DI = 1 so that initial ETU == work etu; 
Also Fi/Di = 372 = Länge der ETU!
TD1 = 0x00 so dass das Lesegerät weiß, dass es hierbei um T=0 geht. 
Mehr Informationen unter: http://www.cardwerk.com/smartcards/smartcard_standard_ISO7816-3.aspx
*/
uint8_t atr_sequence[4] = {0x3B, 0x90, 0x11, 0x00};

//------------------------------------------------------------------------------
//	Status Bytes:
//------------------------------------------------------------------------------
uint8_t statusBytes[2] = {0x61,0x10};		//verschicken nach dem Key Empfang
uint8_t statusBytesAck[2] = {0x90,0x00};		//verschicken nach dem Key Transmit

//------------------------------------------------------------------------------
//	Prototypes:
//------------------------------------------------------------------------------
void uart0_init(void);
void uart0_putchar(unsigned char c);
char uart0_getchar(void);
void wait_tick(uint32_t tick);
void ATR(void);
void startETUTimer(void);
void stopETUTimer(void);
void startTimer2(uint8_t countArg);
void stopTimer2(void);
void transmitByte(uint8_t byte);
void setIOOutput(void);
void setIOInput(void);
void receiveCommandAPDU(void);
uint8_t receiveByte(void);
void uart0_putstring(char *a);
void wait_ticks(uint32_t ticks);
void receiveKey(void);
void transmitStatusBytes(uint8_t *byte);
void transmitC0(void);
void transmitKey(void);

//------------------------------------------------------------------------------
//	Main:
//------------------------------------------------------------------------------
int main (void)
{
	
	//Seed für Rand funktion ist problematisch:
	//man könnte auch auf dem flash gespeicherte werte verwenden, die geändert werden können 
	//ansonsten auch mit Geheimer initialisierung
	srand(13245);
	
	
	//uint8_t test[16] = {  0x00, 0x00, 0x00, 0x00,0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x00, 0x00, 0x00, 0x00 };
	//inv_aes128(test);
	// output should be [197,27,...]	
	uart0_init();
	
	//aes128_init(key); 	//muss nur einmal gemacht werden 
							//man könnte theoretisch auch einfach alle Rundenschlüsssel fest speichern 
							//=>HABS berechnet
	ATR();

	while (1)
	{
		//RECEIVE APDU, RECEIVE KEY (+TRANSMIT CTS BYTES), TRANSMIT FIRST STATUS BYTE
		receiveCommandAPDU();
		receiveKey();
		transmitStatusBytes(statusBytes); 
		
		//RECEIVE SECOND APDU (INS FOR DECRYPT TO FOLLOW) 
		receiveCommandAPDU();
		transmitC0();
		
		//DECRYPT
		for(int i = 0; i < 16; i++)
			state[i] = keyBuffer[i];
		PORTB |= ( 1 << TRIGGER);
		inv_aes128(state);
		PORTB &= ~( 1 << TRIGGER);
		//TRANSMIT KEY
		transmitKey();
		
		//SEND 0x90, 0x00 STATUS BYTES
		transmitStatusBytes(statusBytesAck);
	}
}

//------------------------------------------------------------------------------
//	ATR Function defintions:
//------------------------------------------------------------------------------
void ATR(void)
{
	wait_ticks(1000);		// The I/O set auf Z 100 taktzyklen (muss sicherstellen dass die wait auch richtig wartet) nach dem angelegten CLK.
							// muss laut datenblatt zwischen 400 bis 40000 Takten warten
							
	setIOOutput();			//I/O erstmal auf Z
	
	startETUTimer();        //Lass den Timer bis 372 hochz�hlen, dann interrupt ausl�sen (372/Frequenz ist die ETU)
	transmitLock = 1;
	sei();					//Global Interrupt enable
	
	for(int i=0; i<4; i++)
		transmitByte(atr_sequence[i]);
	
	cli();					//Global Interrupt disable
	
	stopETUTimer();
}

void transmitByte(uint8_t byte)
{
	volatile int8_t parityCounter = 0;
	
	bitToBeSent = 0;					//Start Bit auf LOW
	for (int i = 0; i < 8; i++)
	{
		while(transmitLock);			//Abwarten bis der Timer mit dem Z�hlen neustartet
		bitToBeSent = byte & (1 << i);
		
		if(bitToBeSent)					//Den Parity counter hochz�hlen
			parityCounter++;
		
		transmitLock = 1;				//Nach jedem Verschicken locken, unlock erfolgt erst nach der n�chsten erflogreichen ISR
	}
	
	while(transmitLock);
	
	if(parityCounter % 2 == 1)			//Falls ungerade, ein z�s�tzliches Parity-High verschicken
		bitToBeSent = 1;
	else
		bitToBeSent = 0;				//Ansonsten ein Parity-Low
	
	parityCounter = 0;
	transmitLock = 1;
	while(transmitLock);
	
	bitToBeSent = 1;					//Guard bits nicht vergessen
	for( int i = 0; i < 4; i++)
	{
		while(transmitLock);
		transmitLock = 1;
	}
}

//------------------------------------------------------------------------------
//	Receiving APDU and Key:
//------------------------------------------------------------------------------
/*Ist der ATR erfolgreich abgeschlossen, wartet die Karte auf weitere Instruktionen des Terminals.
Ein solcher Befehl besteht zunächst immer aus fünf Bytes, welche Klasse, Befehl und drei
Befehlsparameter enthält. Abhängig von dieser Bytesequenz wird eine entsprechende Antwort
der Smartcard erwartet.*/
void receiveCommandAPDU()
{
	setIOInput();
	
	sei();

	for(int i = 0; i < 5; i++){
		APDUcommandBuffer[i] = receiveByte();	
		uart0_putchar('a');
	}
	DDRB |=  (1 << DEBUG_IO);
	CLA = APDUcommandBuffer[0];
	INS = APDUcommandBuffer[1];
	PAR1 = APDUcommandBuffer[2];
	PAR2 = APDUcommandBuffer[3];
	PAR3 = APDUcommandBuffer[4];
	
	cli();
}

void receiveKey()
{
	DDRB ^=  (1 << DEBUG_IO);
	readyToReceiveByte = 0xEF;
	wait_ticks(1000);
	sei();
	for(int i=0; i<16; i++)
	{
		wait_ticks(50);
		
		//Zuerst das readyToReceiveByte verschicken, transmit sequenz genauso wie oben
		setIOOutput();			//I/O erstmal auf Z
		startETUTimer();        //Lass den Timer bis 372 hochz�hlen, dann interrupt ausl�sen (372/Frequenz ist die ETU)
		transmitLock = 1;
		transmitByte(readyToReceiveByte);
		
		//Darauffolgend ein keybyte empfangen
		setIOInput();
		stopETUTimer();
		
		keyBuffer[i] = receiveByte();
		
		wait_ticks(1000);
	}
	cli();
}

uint8_t receiveByte()
{
	uint8_t receivedByte = 0x00;    //Das wollen wir ausf�llen
	volatile int8_t parityCounter = 0;
	startBitFlag = 0;				//StartBit immer noch nicht angekommen
	
	PCICR |= (1 << PCIE1);			//Pin Change Interrupt f�r PCINT15:8 anschalten - PORTB
	PCMSK1 |= (1 << PCINT14);		//Pin Change Interrupt nur f�r PCINT14 (PB6) anschalten
	
	while(startBitFlag != 1);		//Abwarten bis das start bit ankommt (muss gleich 0 sein)

	startBitFlag = 0;				//Start bit empfangen, flag auf 0
	PCICR = 0x00;					//Pin CI ausschalten
	PCMSK1 = 0x00;
	
	receiveLock = 1;
	startTimer2(60);					//BEI DER EMPFANGSEQUENZ WIRD DAS STARTBIT L�NGER ALS 372 CYCLES ANGELEGT! AUFPASSEN!
	while(receiveLock);					//Abwarten bis das erste data bit ankommt
	receivedByte |= (receivedBit << 0); //Das erste Bit ist angekommen und in unseres Byte int geschrieben
	
	startTimer2(44);					//Timer der f�r die restlichen 7 bits benutzt wird
	
	if (receivedBit == 1)
		parityCounter++;
	receiveLock = 1;
	
	//Genau das selbe 7 mal noch
    for( int i = 1; i < 8; i++ )
    {
	        while(receiveLock);
	        receivedByte |= (receivedBit << i);
	        if(receivedBit == 1)
		        parityCounter++;
	        receiveLock = 1;
    }
	
	while(receiveLock);				//Letztes Daten Bit hier schon received, trotzdem abwarten bis Timer abgelaufen
	stopTimer2(); 
	
	if(receivedBit)					//Das letzte empfangene Bit ist das Parity Bit
		parityCounter++;
		
	if (parityCounter % 2)
	{
		uart0_putstring("Hmm, irgendwas ist mit den Parity Zeichen schief gelaufen :(");
	}
		
	receiveLock = 1;
	return receivedByte;
}

//------------------------------------------------------------------------------
//	Sending status bytes:
//------------------------------------------------------------------------------
//Laut Analyzer Sample m�ssten sofort nach dem Key-Empfang 2 Status Bytes an den Karenleser verschickt werden
//und zwar 0x61, 0x10...
void transmitStatusBytes(uint8_t *statusBytes)
{
		wait_ticks(1000);		//Zur �bersichtlichkeit
		setIOOutput();
		
		sei();
		startETUTimer();        //Lass den Timer bis 372 hochz�hlen, dann interrupt ausl�sen (372/Frequenz ist die ETU)
		transmitLock = 1;
		
		transmitByte(*statusBytes);
		statusBytes++;
		transmitByte(*statusBytes);
			
		cli();
		stopETUTimer();
}

void transmitC0()
{
		wait_ticks(10000);		//Zur �bersichtlichkeit
		setIOOutput();
		
		sei();
		startETUTimer();
		transmitLock = 1;
		transmitByte(0xC0);
		
		cli();
		stopETUTimer();
}

//------------------------------------------------------------------------------
//	Transmit key:
//------------------------------------------------------------------------------
void transmitKey()
{
		wait_ticks(10000);		//Zur �bersichtlichkeit
		setIOOutput();
			
		sei();
		startETUTimer();
		transmitLock = 1;
		
		for(int i = 0; i<16; i++)
			transmitByte(state[i]);
		
		cli();
		stopETUTimer();
}

//------------------------------------------------------------------------------
//	Initializators:
//------------------------------------------------------------------------------
void setIOOutput(void)
{
	PORTB |= (1 << PDI_DATA);
	DDRB |= (1<<DDB6);
}

void setIOInput(void)
{
	PORTB |= (1 << PDI_DATA);
	DDRB &= ~(1<<DDB6);
}

//------------------------------------------------------------------------------
//	Timers:
//------------------------------------------------------------------------------
// ETUTimer (8 bit Z�hler, Timer0)
void startETUTimer(void)
{
	TCNT0=0;				// Counter auf 0 setzen;
	TCCR0A |= (1 << WGM01); // CTC Modus an um anstelle der durch die Hardware bedingten Obergrenze des Timers, einen anderen Wert zu benutzen!
	TCCR0B |= (1 << CS01);  // Prescaler Wert auf 8 setzen, um mit einer kleineren Taktfrequenz nach oben z�hlen zu k�nnen
	OCR0A = 44;				// CTC bei 44*8 = 372 Taktzyklen
	TIMSK0 |= (1<<OCIE0A); 	// Output Compare Interrupt anschalten
}

void stopETUTimer(void)
{
	TCCR0B = 0x00;
	TIMSK0 = 0x00;
}

//ReceiveTimer (8 bit Z�hler, Timer2)
void startTimer2(uint8_t countArg)
{
	TCNT2 = 0;					// Counter auf 0 setzen;
	TCCR2A |= (1<<WGM21);		// CTC Modus an um anstelle der durch die Hardware bedingten Obergrenze des Timers, einen anderen Wert zu benutzen!
	TCCR2B |= (1<<CS21);		// Prescaler Wert auf 8 setzen, um mit einer kleineren Taktfrequenz nach oben z�hlen zu k�nnen
	OCR2A = countArg;			// CTC bei countArg*8 = X Taktzyklen
	TIMSK2 |= (1<<OCIE2A);		// Output Compare Interrupt anschalten
}

void stopTimer2()
{
	TCCR0A &= ~(1 << WGM01);
	TCCR2B = 0x00;
	TIMSK2 = 0x00;
}

//------------------------------------------------------------------------------
//	Interrupt Service Routines:
//------------------------------------------------------------------------------
// ETUTimer ISR
ISR(TIMER0_COMPA_vect)
{
	cli();					//Interrupts ausschalten
	if(!bitToBeSent)
		PORTB &= ~(1 << PDI_DATA);
	else
		PORTB |= (1 << PDI_DATA);
	transmitLock = 0;
	sei();					//Interrupts anschalten
}

//Pin Change Interrupt - f�r PCINT14
ISR(PCINT1_vect)
{
	cli();										//Interrupts ausschalten
	if( ( PINB & (1 << PDI_DATA) ) == 0 )		//Wenn 0 anliegt...
		startBitFlag = 1;
	sei();										//Interrupts anschalten
}

// Receiving ISR (Timer 2)
ISR(TIMER2_COMPA_vect)
{
	cli();
	if( ( PINB & ( 1 << PDI_DATA ) ) == 0 )
		receivedBit = 0;
	else
		receivedBit = 1;
		
	receiveLock = 0;
	sei();
}
//------------------------------------------------------------------------------
//	UART Function defintions:
//------------------------------------------------------------------------------
void uart0_init(void)
{
	//Set Baud Rate:
	//USART0 Baud Rate Register High Byte:
	UBRR0H=(uint8_t)((F_CPU / (16 * baudrate)) - 1)>>8;
	//USART0 Baud Rate Register Low Byte
	UBRR0L=(uint8_t)(F_CPU / (16 * baudrate)) - 1;


	// Asynchron 8N1
	UCSR0C = (1<<UCSZ01) | (1<<UCSZ00);   
	
	 // tx & rx enable (Receiver and Transmitter)
	UCSR0B = (1<<TXEN0) | (1<<RXEN0);    
}

void uart0_putchar(unsigned char c)
{
	while(!(UCSR0A & (1<<UDRE0)));
	// loop_until_bit_is_set( UCSR0A, UDRE0 ); // auch m�glich
	UDR0 = c; //USART0 I/O Data Register 
}

void uart0_putstring(char *a)
{
	while(*a != 0){
		uart0_putchar(*a);
		a++;
	}
}

char uart0_getchar(void)
{
	while(!(UCSR0A & (1<<RXC0)))
		;
	// loop_until_bit_is_set( UCSR0A, RXC0 );
	// es gibt auch loop_until_bit_is_clear

	return( UDR0 );
}

//------------------------------------------------------------------------------
//	Sonstige:
//------------------------------------------------------------------------------
void wait_ticks(uint32_t ticks)
{
	
	// hmm, bin mir nicht sicher wieviel Takte gewartet werden
	// wegen der Subtraktion und so....
	while(ticks--){
		asm volatile ("nop");
	}
	
}
