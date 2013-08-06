smartcard-aes-fw
================
### Introduction:
A generic SmartCard firmware implementation allowing for communication based on ISO 7816 Part 3/Part 4 protocol standards, incorporating safe AES-128 with masking and shuffling as security measures. The solely look-up based (inverse) AES is used for decryption purposes.

### Implementation:
In order to achieve the functionality of the original card, the clone card needed to be ISO7816-3 compatible, that is, support the T=0 data transfer protocol. The base implementation revolves around continuous execution of states, and thus could be realized as a state machine, consisting of an ATR (Answer to Reset) procedure, followed by a loop receive procedure (for the command APDUs and the 16-byte key sequence), a decryption procedure (128-bit inverse AES) and, finally, the transmission of the decrypted key sequence. The ATR sequence as well as the receive and transmission procedures are described in detail further below.

<a href="http://imgur.com/1PBSZNk"><img src="http://i.imgur.com/1PBSZNk.png"/></a>

### ATR (Answer To Reset sequence): 
In principle, the bidirectional communication, where the card reader and the target card are the communication partners, can begin only after the Answer To Reset sequence, sent by the card to the card reader, has been correctly parsed and understood by the latter. The Answer to Reset sequence is followed by setting the RST (reset) pin on the card to HIGH by the card reader. The minimal sequence consists of four bytes, these being {0x3B ,0x90, 0x11, 0x00}. The initial character 0x3B provides a bit shynchronisation sequence and defines the conventions on how to code data bytes in all subsequent characters. In our case, direct convention is used, where logic level ONE is represented by the tristate-state Z. The format character 0x90 indicates the presence of the subsequent characters TA1 and TD1, as well as the number of historical characters (here zero). The four least significant bits of the interface character TD1 (in our case, TD1 equals 0x00) indicate the protocol type T=0, specifying rules to be used to process transmission protocols. The TA1=0x11 interface byte determines the length of the work elementary time units (ETU) length. The ETU represents the nominal bit duration used in a character frame. The TA1 interface byte sets the clock rate conversion factor to 1 (equals 372 in the conversion table), and the bit rate adjustment factor D to 1. The equation for computing the final work ETU is:

`Work_ETU = (1/D)*(F/f) sec`

The externally provided clock equals f~3,4MHz. Setting the D to 1 and Fi to 1 (F=372) results with the work ETU duration equalling the initial ETU duration.

### APDU (Application Protocol Data Unit) command and response: 
After the ATR has been successfully parsed by the Card Reader device, an APDU is sent out to the target card, consisting of 5 bytes, which are the Class, Instruction (INS) bytes and 3 additional parameters. The card then sends the received INS byte back to the receiver, acknowledging the successful transmission and demanding for the 16 ciphertext-bytes to be sent to it. After a successful transmission, the card will answer with a status byte sequence {0x61,0x10}, acknowledging the successful receive of the 16 (0x10) bytes. The target card then waits for the card reader to send out a new APDU, demanding for the start of the decryption procedure and transmission of the plaintext key to the card reader. After the receive of the second APDU, the 128-bit AES decryption function is called, using the master key identified by the DPA Team, the previously received 16 cypthertext bytes decrypted, and the plaintext session key sent to the card reader. Finally, the successful transmission is acknowledged once again by the target card, by sending the status byte sequence {0x90,0x00} to the card reader.

In order to achieve the functionality of the bit duration of 372 clock cycles, the timer was needed, counting up to 372 and resulting with a Output Compare Match interrupt. Since an eight-bit timer was used, we utilized the prescaler of eight in order to increment the timer counter only every eight clock cycles. This would then result with a timer interrupt as soon as the counter timer hit 44 (since 372/8 == 44). The timer interrupts were then used when determining the duration of the time a bit is available for reading and writing on a pin. A second eight-bit timer was used in order to completely part the receive and transmit sequence functions. Basically, each 372 clock cycles, a timer interrupt would result with executing an Interrupt Service Routine (ISR). In the ISR function, the locking flag is set to 0, a bit lying on the PDI_DATA pin read in by the card and appended to the already received payload (RX), or the next bit in the bit-sequence written on the pin (TX). The locking flag is then set to 1, resulting with a simple locking mechanism blocking the next read out/write out operations until the timer ISR is executed again, after another ETU (372 clock cycles) has passed. A basic frame consists of a start bit, 8 data bits (1-byte payload), a parity bit and four guard bits.

### Transmission:

In order to achieve the functionality of the bit duration of 372 clock cycles, the timer was needed, counting up to 372 and resulting with a Output Compare Match interrupt. Since an eight-bit timer was used, we utilized the prescaler of eight in order to increment the timer counter only every eight clock cycles. This would then result with a timer interrupt as soon as the counter timer hit 44 (since 372/8 == 44). The timer interrupts were then used when determining the duration of the time a bit is available for reading and writing on a pin. A second eight-bit timer was used in order to completely part the receive and transmit sequence functions. Basically, each 372 clock cycles, a timer interrupt would result with executing an Interrupt Service Routine (ISR). In the ISR function, the locking flag is set to 0, a bit lying on the PDI_DATA pin read in by the card and appended to the already received payload (RX), or the next bit in the bit-sequence written on the pin (TX). The locking flag is then set to 1, resulting with a simple locking mechanism blocking the next read out/write out operations until the timer ISR is executed again, after another ETU (372 clock cycles) has passed. A basic frame consists of a start bit, 8 data bits (1-byte payload), a parity bit and four guard bits.

<a href="http://imgur.com/4sG8efI"><img src="http://i.imgur.com/4sG8efI.png"/></a>

The described control flow looks as follows, the transmit (write on PDI_DATA) functionality is analog to the receive function's control flow:
      
      
    receiveLock = 1;      
    for( int i = 0; i < 8; i++ )
    {
      while(receiveLock);
    	receivedByte |= (receivedBit << i);
	    if(receivedBit == 1)
	        parityCounter++;
	    receiveLock = 1;
    }
    
... and the corresponding Timer ISR:  

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

On the other hand, when receiving a payload, the start bit (LOW) on the PDI_DATA ping has to be distinguished from the idle high state the pin is normally exposed to. For this purpose, the pin change interrupt was used, and an ISR executed whenever the pin value PDI_DATA changed from idle mode:
       
        
       ISR(PCINT1_vect)
       {
	    cli();						//turn off interrupts
	    if( ( PINB & (1 << PDI_DATA) ) == 0 )		//if LOW was observed
		startBitFlag = 1;				//set start bit flag to 1
	    sei();						//turn interrupts on
       } 
       
       
This ISR sets the start bit flag to 1 and allows for distinguishing the incoming sequence of bits which are to be received and saved into the byte variable receivedByte.
       
         
       uint8_t receiveByte()
       {
	  uint8_t receivedByte = 0x00;    	//the byte to be filled
	  startBitFlag = 0;			//startBit has still not arrived
	
	  PCICR |= (1 << PCIE1);			//turn on the Pin Change Interrupt for PCINT15:8 - PORTB
	  PCMSK1 |= (1 << PCINT14);		//turn on the Pin Change Interrupt only for PCINT14 (PB6)
	
	  while(startBitFlag != 1);		//wait until a start bit shows up

	  startBitFlag = 0;			//start bit received (PCINT ISR), set flag to 0
       }
	
### Masking Implementation: 
The main idea of masking is the processing of all relevant information (the AES decryption) behind a bitmask. Therefore 6 random values are generated: m, m', m1, m2, m3, m4. But often m and m' are identical (in some papers and also in the book from Mr. Mangard). With an identical mask m and m', it is more easy to attack the masked implementation with a second order DPA. The secure implementation uses different values for m and m' but for testing/attacking simplifications, it was also implemented with same masks. For the "Mix Column" operation, 4 outputmasks m1', m2', m3', m4' have to be precalculated based on the input masks m1, m2, m3, m4. There is also a difference between the AES encryption and decryption, therefore the sequence of the applied masks had to be used in an appropriate order.

<a href="http://imgur.com/PGffcAo"><img src="http://i.imgur.com/PGffcAo.png"/></a>

### Shuffling Implementation: 

Main idea of shuffling is to lower the discoverable correlation coefficient by executing the DPA attack. In the shuffling implementation, the S-Box lookups are processed in an arbitrary order. This already reduces the correlation coefficient (if no further steps are taken in the DPA) by the factor 16. Furthermore, the oder of the operations "Shift Rows" and "S-Box Lookup" is processed in arbitrary order, because the two operations are interchangeable. This causes a reduces correlation coefficient by the faktor 2*16 = 32 (In case no windowing attack is considered).

The C-Code for the random sequence generation:

      uint8_t indi[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};		
      for (int i = 15; i >= 0 ;i--){
          int val = rand() % (i+1);
          hiding_sequence[i] = indi[val]; 
          for( int j = val; j < i ; j++){ //shifting all remaining values left:
              indi[j] = indi[j+1];			
          }
      }
The C-Code for the random S-box Lookups:

      for (uint8_t i = 0; i < 16; i++){
          state[hiding_sequence[i]] = InvSbox_masked[state[hiding_sequence[i]]];
      }
The C-Code for shuffling Sub-Bytes and Shift-Rows Operations:

      if(rand()%2 == 1)
      {
          inv_subBytes_masked_rand(state,hiding_sequence);
          inv_shiftRows(state);
      }
      else
      {
          inv_shiftRows(state);
          inv_subBytes_masked_rand(state,hiding_sequence);
      }

### Reference standard: 
http://www.cardwerk.com/smartcards/smartcard_standard_ISO7816-3.aspx [Electronic Signals and Transmission Protocols]  
http://www.cardwerk.com/smartcards/smartcard_standard_ISO7816-4.aspx [Interindustry Commands for Interchange]  
http://books.google.com/books/about/Power_Analysis_Attacks.html?id=YXkASFjeUswC [Power Analysis Attacks: Revealing the Secrets of Smart Cards (Google eBook)]
