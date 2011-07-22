
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

//The source switch/(volume/tone control) IC uses a sanyo-proprietary
//protocol called "CCB" which is just kinda similar to SPI. The chip has
//three data lines: CE (chip enable), DI (data input) and CL (clock). We
//intercept all three. "our" "master" is connnected to the AVR's hardware
//SPI interface for no good reason. Well, in fact for one good reason: So
//nobody needs to care about the master's speediness.
//NOTE: CCB uses some strange address byte (constant for one chip type) which is
//sent before CE is pulled high. We may safely ignore this byte since it is
//the same everytime.

#define MASTER_CS_INPUT	PINC
#define MASTER_CS_PIN	3

#define SLAVE_DI_DDR	DDRC
#define SLAVE_DI_PORT	PORTC
#define SLAVE_DI_PIN	2

#define SLAVE_CLK_DDR	DDRC
#define SLAVE_CLK_PORT	PORTC
#define SLAVE_CLK_PIN	1

#define SLAVE_CE_DDR	DDRC
#define SLAVE_CE_PORT	PORTC
#define SLAVE_CE_PIN	0

//SourceSelectSwitch!
#define SSS_INPUT		PINC
#define SSS_PIN			4

//Master CS pin
#define LAST_MASTER_CS	0x80

uint8_t state = 0;

uint8_t ccb_data[4];
uint8_t ccb_data_position;

void send_ccb_command(void){
	for(uint8_t sent=255; sent!=3; sent++){//Nasty, but makes the array indexing more easy.
		uint8_t data;
		if(sent == 255){
			data = 0x82; //The LC75342 address
		}else{
			SLAVE_CE_PORT |= _BV(SLAVE_CE_PIN);
			data = ccb_data[4];
		}
		for(uint8_t bit = 0x01; bit!=0; bit=bit<<1){
			//Set DI
			if(data&bit)
				SLAVE_DI_PORT |= _BV(SLAVE_DI_PIN);
			else
				SLAVE_DI_PORT &=~_BV(SLAVE_DI_PIN);
			//Toggle CLK once *slowly*.
			_delay_us(2); //See spec. These values are actually too high.
			SLAVE_CLK_PORT |= _BV(SLAVE_CLK_PIN);
			_delay_us(2);
			SLAVE_CLK_PORT &=~_BV(SLAVE_CLK_PIN);
		}
		if(sent == 255){
			_delay_us(2); //As per spec (see datasheet)
		}
	}
	SLAVE_CE_PORT &=~_BV(SLAVE_CE_PIN);
}

void set_to_aux(void){
	ccb_data[0] = 0x03; //Input gain 0dB; input 4
	ccb_data[1] = 0x00; //Volume +-0dB
	ccb_data[2] = 0x00; //Treble +-0dB; Bass +-0dB (part I)
	ccb_data[3] = 0x0C; //Test flags, apply these settings to left and right channel, Bass +-0dB (part II)
	send_ccb_command();
}

void poll_and_repeat_ccb(void){
	if(MASTER_CS_INPUT & _BV(MASTER_CS_INPUT)){
		if(!(state & LAST_MASTER_CS)){
			//MASTER_CS just went high.
			state |= LAST_MASTER_CS;
			ccb_data_position = 0;
		}
		if(SPSR & SPIF){//FIXME data received
			ccb_data[ccb_data_position] = SPDR;
			ccb_data_position++;
			ccb_data_position &= 0xFC;
			if(!ccb_data_position){//Transfer complete, forward!
				send_ccb_command();
			}
		}
	}else
		state &=~ LAST_MASTER_CS;
}

int main(void){
	//Device initialization
	//Ports
	SLAVE_DI_DDR |= _BV(SLAVE_DI_PIN);
	SLAVE_CLK_DDR |= _BV(SLAVE_DI_PIN);
	SLAVE_CE_DDR |= _BV(SLAVE_DI_PIN);
	//Enable SPI
	//LSB first, falling edge leading, trailing edge sample, interrupt disabled,
	//slave mode
	SPCR = 0x6C;
	//Main loop. Will never exit.
	for(;;){
		if(SSS_INPUT & _BV(SSS_PIN)){ //Switch polling
			poll_and_repeat_ccb();
		}else{//AUX_INPUT
			//Hopefully this does not inject noise or something...
			set_to_aux();
		}
		_delay_us(42);
	}
	return 23;
}

