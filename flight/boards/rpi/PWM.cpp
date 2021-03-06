/*
 * BCFlight
 * Copyright (C) 2016 Adrien Aubry (drich)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700

#include <execinfo.h>
#include <signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "mailbox.h"
#include <Debug.h>
#include "PWM.h"

// pin2gpio array is not setup as empty to avoid locking all GPIO
// inputs as PWM, they are set on the fly by the pin param passed.
// static uint8_t pin2gpio[8];

#define DEVFILE			"/dev/pi-blaster"
#define DEVFILE_MBOX    "/dev/pi-blaster-mbox"
#define DEVFILE_VCIO	"/dev/vcio"

#define PAGE_SIZE		4096
#define PAGE_SHIFT		12

#define DMA_BASE		(periph_virt_base + 0x00007000)
#define DMA_CHAN_SIZE	0x100 /* size of register space for a single DMA channel */
#define DMA_CHAN_MAX	14  /* number of DMA Channels we have... actually, there are 15... but channel fifteen is mapped at a different DMA_BASE, so we leave that one alone */
#define PWM_BASE_OFFSET 0x0020C000
#define PWM_BASE		(periph_virt_base + PWM_BASE_OFFSET)
#define PWM_PHYS_BASE	(periph_phys_base + PWM_BASE_OFFSET)
#define PWM_LEN			0x28
#define CLK_BASE_OFFSET 0x00101000
#define CLK_BASE		(periph_virt_base + CLK_BASE_OFFSET)
#define CLK_LEN			0xA8
#define GPIO_BASE_OFFSET 0x00200000
#define GPIO_BASE		(periph_virt_base + GPIO_BASE_OFFSET)
#define GPIO_PHYS_BASE	(periph_phys_base + GPIO_BASE_OFFSET)
#define GPIO_LEN		0x100

#define DMA_NO_WIDE_BURSTS	(1<<26)
#define DMA_WAIT_RESP		(1<<3)
#define DMA_D_DREQ		(1<<6)
#define DMA_PER_MAP(x)		((x)<<16)
#define DMA_END			(1<<1)
#define DMA_RESET		(1<<31)
#define DMA_INT			(1<<2)

#define DMA_CS			(0x00/4)
#define DMA_CONBLK_AD		(0x04/4)
#define DMA_DEBUG		(0x20/4)

#define GPIO_FSEL0		(0x00/4)
#define GPIO_SET0		(0x1c/4)
#define GPIO_CLR0		(0x28/4)
#define GPIO_LEV0		(0x34/4)
#define GPIO_PULLEN		(0x94/4)
#define GPIO_PULLCLK		(0x98/4)

#define GPIO_MODE_IN		0
#define GPIO_MODE_OUT		1

#define PWM_CTL			(0x00/4)
#define PWM_PWMC		(0x08/4)
#define PWM_RNG1		(0x10/4)
#define PWM_FIFO		(0x18/4)

#define PWMCLK_CNTL		40
#define PWMCLK_DIV		41

#define PWMCTL_MODE1		(1<<1)
#define PWMCTL_PWEN1		(1<<0)
#define PWMCTL_CLRF		(1<<6)
#define PWMCTL_USEF1		(1<<5)

#define PWMPWMC_ENAB		(1<<31)
#define PWMPWMC_THRSHLD		((15<<8)|(15<<0))

/* New Board Revision format: 
SRRR MMMM PPPP TTTT TTTT VVVV

S scheme (0=old, 1=new)
R RAM (0=256, 1=512, 2=1024)
M manufacturer (0='SONY',1='EGOMAN',2='EMBEST',3='UNKNOWN',4='EMBEST')
P processor (0=2835, 1=2836)
T type (0='A', 1='B', 2='A+', 3='B+', 4='Pi 2 B', 5='Alpha', 6='Compute Module')
V revision (0-15)
*/
#define BOARD_REVISION_SCHEME_MASK (0x1 << 23)
#define BOARD_REVISION_SCHEME_OLD (0x0 << 23)
#define BOARD_REVISION_SCHEME_NEW (0x1 << 23)
#define BOARD_REVISION_RAM_MASK (0x7 << 20)
#define BOARD_REVISION_MANUFACTURER_MASK (0xF << 16)
#define BOARD_REVISION_MANUFACTURER_SONY (0 << 16)
#define BOARD_REVISION_MANUFACTURER_EGOMAN (1 << 16)
#define BOARD_REVISION_MANUFACTURER_EMBEST (2 << 16)
#define BOARD_REVISION_MANUFACTURER_UNKNOWN (3 << 16)
#define BOARD_REVISION_MANUFACTURER_EMBEST2 (4 << 16)
#define BOARD_REVISION_PROCESSOR_MASK (0xF << 12)
#define BOARD_REVISION_PROCESSOR_2835 (0 << 12)
#define BOARD_REVISION_PROCESSOR_2836 (1 << 12)
#define BOARD_REVISION_TYPE_MASK (0xFF << 4)
#define BOARD_REVISION_TYPE_PI1_A (0 << 4)
#define BOARD_REVISION_TYPE_PI1_B (1 << 4)
#define BOARD_REVISION_TYPE_PI1_A_PLUS (2 << 4)
#define BOARD_REVISION_TYPE_PI1_B_PLUS (3 << 4)
#define BOARD_REVISION_TYPE_PI2_B (4 << 4)
#define BOARD_REVISION_TYPE_ALPHA (5 << 4)
#define BOARD_REVISION_TYPE_CM (6 << 4)
#define BOARD_REVISION_REV_MASK (0xF)

#define LENGTH(x)  (sizeof(x) / sizeof(x[0]))

#define BUS_TO_PHYS(x) ((x)&~0xC0000000)

#define DEBUG 1
#ifdef DEBUG
#define dprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...)
#endif

std::vector< PWM::Channel* > PWM::mChannels = std::vector< PWM::Channel* >();

static void fatal( const char *fmt, ... )
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	exit(0);
}


PWM::PWM( uint32_t pin, uint32_t time_base, uint32_t period_time, uint32_t sample_time )
	: mChannel( nullptr )
	, mPin( pin )
{
	uint8_t channel = 14;
	for ( Channel* chan : mChannels ) {
		if ( chan->mTimeBase == time_base and chan->mCycleTime == period_time and chan->mSampleTime == sample_time ) {
			mChannel = chan;
			break;
		}
		channel = chan->mChannel - 1;
	}
	if ( not mChannel ) {
		mChannel = new Channel( channel, time_base, period_time, sample_time );
		mChannels.emplace_back( mChannel );
	}
}


PWM::~PWM()
{
}

void PWM::SetPWMus( uint32_t width_us )
{
	mChannel->SetPWMus( mPin, width_us );
}


void PWM::Update()
{
	mChannel->Update();
}


PWM::Channel::Channel( uint8_t channel, uint32_t time_base, uint32_t period_time, uint32_t sample_time )
	: mChannel( channel )
	, mPinsCount( 0 )
	, mTimeBase( time_base )
	, mCycleTime( period_time )
	, mSampleTime( sample_time )
	, mNumSamples( mCycleTime / mSampleTime )
	, mNumCBs( mNumSamples * 2 )
	, mNumPages( ( mNumCBs * sizeof(dma_cb_t) + mNumSamples * 4 + PAGE_SIZE - 1 ) >> PAGE_SHIFT )
{
	memset( mPins, 0, sizeof(mPins) );
	memset( mPinsPWM, 0, sizeof(mPinsPWM) );
	memset( mPinsMode, 0, sizeof(mPinsMode) );
	memset( mPinsBuffer, 0, sizeof(mPinsBuffer) );
	memset( mPinsBufferLength, 0, sizeof(mPinsBufferLength) );

	mMbox.handle = mbox_open();
	if (mMbox.handle < 0) {
		fatal("Failed to open mailbox\n");
	}

	uint32_t mbox_board_rev = get_board_revision( mMbox.handle );
	gDebug() << "MBox Board Revision: " << mbox_board_rev << "\n";
	get_model(mbox_board_rev);
	uint32_t mbox_dma_channels = get_dma_channels( mMbox.handle );
	gDebug() << "PWM Channels Info: " << mbox_dma_channels << ", using PWM Channel: " << (int)mChannel << "\n";

	gDebug() << "Number of channels:             " << (int)mPinsCount << "\n";
	gDebug() << "PWM frequency:               " << time_base / mCycleTime << "\n";
	gDebug() << "PWM steps:                      " << mNumSamples << "\n";
	gDebug() << "PWM step :                    " << mSampleTime << "\n";
	gDebug() << "Maximum period (100  %%):      " << mCycleTime << "\n";
	gDebug() << "Minimum period (" << 100.0 * mSampleTime / mCycleTime << "):      " << mSampleTime << "\n";
	gDebug() << "DMA Base:                  " << DMA_BASE << "\n";

// 	setup_sighandlers();

	/* map the registers for all PWM Channels */
	dma_virt_base = (uint32_t*)map_peripheral( DMA_BASE, ( DMA_CHAN_SIZE * ( DMA_CHAN_MAX + 1 ) ) );
	/* set dma_reg to point to the PWM Channel we are using */
	dma_reg = dma_virt_base + mChannel * ( DMA_CHAN_SIZE / sizeof(dma_reg) );
	pwm_reg = (uint32_t*)map_peripheral( PWM_BASE, PWM_LEN );
	clk_reg = (uint32_t*)map_peripheral( CLK_BASE, CLK_LEN );
	gpio_reg = (uint32_t*)map_peripheral( GPIO_BASE, GPIO_LEN );

	/* Use the mailbox interface to the VC to ask for physical memory */
	mMbox.mem_ref = mem_alloc( mMbox.handle, mNumPages * PAGE_SIZE, PAGE_SIZE, mem_flag );
	dprintf( "mem_ref %u\n", mMbox.mem_ref );
	mMbox.bus_addr = mem_lock( mMbox.handle, mMbox.mem_ref );
	dprintf( "bus_addr = %#x\n", mMbox.bus_addr );
	mMbox.virt_addr = (uint8_t*)mapmem( BUS_TO_PHYS(mMbox.bus_addr), mNumPages * PAGE_SIZE );
	dprintf( "virt_addr %p\n", mMbox.virt_addr );
	mCtl.sample = (uint32_t*)mMbox.virt_addr;
	mCtl.cb = (dma_cb_t*)&mMbox.virt_addr[ sizeof(uint32_t) * mNumSamples ];

	if ( (unsigned long)mMbox.virt_addr & ( PAGE_SIZE - 1 ) ) {
		fatal("pi-blaster: Virtual address is not page aligned\n");
	}

	/* we are done with the mbox */
	close( mMbox.handle );
	mMbox.handle = -1;

	init_ctrl_data();
	init_hardware( time_base );
	update_pwm();
}


PWM::Channel::~Channel()
{
}


void PWM::Channel::SetPWMus( uint32_t pin, uint32_t width_us )
{
	float fwidth = ( (float)width_us / (float)mCycleTime );

	bool found = false;
	uint32_t i = 0;
	for ( i = 0; i < mPinsCount; i++ ) {
		if ( mPins[i] == pin ) {
			found = true;
			break;
		}
	}
	if ( not found ) {
		i = mPinsCount;
		mPinsCount += 1;
		mPins[i] = pin;
	}

	if ( mPinsMode[i] != MODE_PWM ) {
		mPinsMode[i] = MODE_PWM;
		gpio_reg[GPIO_CLR0] = 1 << pin;
		uint32_t fsel = gpio_reg[GPIO_FSEL0 + pin/10];
		fsel &= ~(7 << ((pin % 10) * 3));
		fsel |= GPIO_MODE_OUT << ((pin % 10) * 3);
		gpio_reg[GPIO_FSEL0 + pin/10] = fsel;
	}
	mPinsPWM[i] = fwidth;
}


void PWM::Channel::SetPWMBuffer( uint32_t pin, uint8_t* buffer, uint32_t len )
{
	bool found = false;
	uint32_t i = 0;
	for ( i = 0; i < mPinsCount; i++ ) {
		if ( mPins[i] == pin ) {
			found = true;
			break;
		}
	}
	if ( not found ) {
		i = mPinsCount;
		mPinsCount += 1;
	}

	mPinsMode[i] = MODE_BUFFER;
	if ( mPinsBuffer[i] and mPinsBufferLength[i] < len ) {
		delete mPinsBuffer[i];
		mPinsBuffer[i] = nullptr;
	}
	if ( not mPinsBuffer[i] ) {
		mPinsBuffer[i] = new uint8_t[ len ];
	}
	memcpy( mPinsBuffer[i], buffer, len );
	mPinsBufferLength[i] = len;
}


void PWM::Channel::Update()
{
	update_pwm();
}


void PWM::Channel::update_pwm()
{
	uint32_t phys_gpclr0 = GPIO_PHYS_BASE + 0x28;
	uint32_t phys_gpset0 = GPIO_PHYS_BASE + 0x1c;
	uint32_t mask;

	uint32_t i, j;
	/* First we turn on the channels that need to be on */
	/*   Take the first PWM Packet and set it's target to start pulse */
	mCtl.cb[0].dst = phys_gpset0;

	/*   Now create a mask of all the pins that should be on */
	mask = 0;
	for ( i = 0; i <= mPinsCount; i++ ) {
		mask |= 1 << mPins[i];
	}
	/*   And give that to the PWM controller to write */
	mCtl.sample[0] = mask;

	/* Now we go through all the samples and turn the pins off when needed */
	for ( j = 1; j < mNumSamples; j++ ) {
		mCtl.cb[j*2].dst = phys_gpclr0;
		mask = 0;
		for ( i = 0; i <= mPinsCount; i++ ) {
			if ( mPins[i] and j > (uint32_t)( mPinsPWM[i] * mNumSamples ) ) {
				mask |= 1 << mPins[i];
			}
			/*
			if ( mPins[i] ) {
				if ( mPinsMode[i] == MODE_PWM and j > (uint32_t)( mPinsPWM[i] * mNumSamples ) ) {
					mask |= 1 << mPins[i];
				} else if ( mPinsMode[i] == MODE_BUFFER ) {
					if ( ( j - 1 ) < mPinsBufferLength[i] and mPinsBuffer[i] and mPinsBuffer[i][j-1] == 0 ) {
						mCtl.cb[j*2].dst = phys_gpclr0;
						mask |= 1 << mPins[i];
					} else {
						mCtl.cb[j*2].dst = phys_gpset0;
						mask |= 1 << mPins[i];
					}
				} else {
				}
			}
			*/
		}
		mCtl.sample[j] = mask;
	}
}


void PWM::Channel::init_ctrl_data()
{
	dprintf( "Initializing PWM ...\n" );
	dma_cb_t *cbp = mCtl.cb;
	uint32_t phys_fifo_addr;
	uint32_t phys_gpclr0 = GPIO_PHYS_BASE + 0x28;
	uint32_t mask;
	uint32_t i;

	phys_fifo_addr = PWM_PHYS_BASE + 0x18;
	memset( mCtl.sample, 0, sizeof(uint32_t)*mNumSamples );

	// Calculate a mask to turn off all the servos
	mask = 0;
	for ( i = 0; i < mPinsCount; i++ ) {
		mask |= 1 << mPins[i];
	}
	for ( i = 0; i < mNumSamples; i++ ) {
		mCtl.sample[i] = mask;
	}

	/* Initialize all the PWM commands. They come in pairs.
	 *  - 1st command copies a value from the sample memory to a destination
	 *    address which can be either the gpclr0 register or the gpset0 register
	 *  - 2nd command waits for a trigger from an PWM source
	 */
	for (i = 0; i < mNumSamples; i++) {
		/* First PWM command */
		cbp->info = DMA_NO_WIDE_BURSTS | DMA_WAIT_RESP;
		cbp->src = mem_virt_to_phys(mCtl.sample + i);
		cbp->dst = phys_gpclr0;
		cbp->length = 4;
		cbp->stride = 0;
		cbp->next = mem_virt_to_phys(cbp + 1);
		cbp++;
		/* Second PWM command */
		cbp->info = DMA_NO_WIDE_BURSTS | DMA_WAIT_RESP | DMA_D_DREQ | DMA_PER_MAP(5);
		cbp->src = mem_virt_to_phys(mCtl.sample);	// Any data will do
		cbp->dst = phys_fifo_addr;
		cbp->length = 4;
		cbp->stride = 0;
		cbp->next = mem_virt_to_phys(cbp + 1);
		cbp++;
	}
	cbp--;
	cbp->next = mem_virt_to_phys(mCtl.cb);
	dprintf( "Ok\n" );
}


void PWM::Channel::init_hardware( uint32_t time_base )
{
	dprintf("Initializing PWM HW...\n");

	// Initialise PWM
	pwm_reg[PWM_CTL] = 0;
	usleep(10);
	clk_reg[PWMCLK_CNTL] = 0x5A000006;		// Source=PLLD (500MHz)
	usleep(100);
// 	clk_reg[PWMCLK_DIV] = 0x5A000000 | ( 500 << 12 );	// set pwm div to 500, giving 1MHz
	clk_reg[PWMCLK_DIV] = 0x5A000000 | ( ( 500000000 / time_base ) << 12 );
	usleep(100);
	clk_reg[PWMCLK_CNTL] = 0x5A000016;		// Source=PLLD and enable
	usleep(100);
	pwm_reg[PWM_RNG1] = mSampleTime;
	usleep(10);
	pwm_reg[PWM_PWMC] = PWMPWMC_ENAB | PWMPWMC_THRSHLD;
	usleep(10);
	pwm_reg[PWM_CTL] = PWMCTL_CLRF;
	usleep(10);
	pwm_reg[PWM_CTL] = PWMCTL_USEF1 | PWMCTL_PWEN1;
	usleep(10);

	// Initialise the PWM
	dma_reg[DMA_CS] = DMA_RESET;
	usleep(10);
	dma_reg[DMA_CS] = DMA_INT | DMA_END;
	dma_reg[DMA_CONBLK_AD] = mem_virt_to_phys(mCtl.cb);
	dma_reg[DMA_DEBUG] = 7; // clear debug error flags
	dma_reg[DMA_CS] = 0x10880001;	// go, mid priority, wait for outstanding writes
	dprintf( "Ok\n" );
}


int PWM::Channel::mbox_open()
{
	int file_desc;

	// try to use /dev/vcio first (kernel 4.1+)
	file_desc = open( DEVFILE_VCIO, 0 );
	if ( file_desc < 0 ) {
		unlink( DEVFILE_MBOX );
		if ( mknod( DEVFILE_MBOX, S_IFCHR | 0600, makedev( MAJOR_NUM, 0 ) ) < 0 ) {
			fatal("Failed to create mailbox device\n");
		}
		file_desc = open( DEVFILE_MBOX, 0 );
		if ( file_desc < 0 ) {
			printf( "Can't open device file: %s\n", DEVFILE_MBOX );
			perror( NULL );
			exit( -1 );
		}
	}
	return file_desc;
}


uint32_t PWM::Channel::mem_virt_to_phys( void* virt )
{
	uint32_t offset = (uint8_t*)virt - mMbox.virt_addr;
	return mMbox.bus_addr + offset;
}


void PWM::Channel::get_model( unsigned mbox_board_rev )
{
	int board_model = 0;

	if ( ( mbox_board_rev & BOARD_REVISION_SCHEME_MASK ) == BOARD_REVISION_SCHEME_NEW ) {
		if ( ( mbox_board_rev & BOARD_REVISION_TYPE_MASK ) == BOARD_REVISION_TYPE_PI2_B ) {
			board_model = 2;
		} else {
			// no Pi 2, we assume a Pi 1
			board_model = 1;
		}
	} else {
		// if revision scheme is old, we assume a Pi 1
		board_model = 1;
	}

	switch ( board_model ) {
		case 1:
			periph_virt_base = 0x20000000;
			periph_phys_base = 0x7e000000;
			mem_flag         = MEM_FLAG_L1_NONALLOCATING | MEM_FLAG_ZERO;
			break;
		case 2:
			periph_virt_base = 0x3f000000;
			periph_phys_base = 0x7e000000;
			mem_flag         = MEM_FLAG_L1_NONALLOCATING | MEM_FLAG_ZERO; 
			break;
		default:
			fatal( "Unable to detect Board Model from board revision: %#x", mbox_board_rev );
			break;
	}
}

void* PWM::Channel::map_peripheral( uint32_t base, uint32_t len )
{
	int fd = open( "/dev/mem", O_RDWR | O_SYNC );
	void * vaddr;

	if ( fd < 0 ) {
		fatal( "pi-blaster: Failed to open /dev/mem: %m\n" );
	}
	vaddr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base );
	if ( vaddr == MAP_FAILED ) {
		fatal("pi-blaster: Failed to map peripheral at 0x%08x: %m\n", base );
	}
	close( fd );

	return vaddr;
}
