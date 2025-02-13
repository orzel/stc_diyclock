// DS1302 RTC IC
// http://datasheets.maximintegrated.com/en/ds/DS1302.pdf
//

#pragma callee_saves sendbyte,readbyte
#pragma callee_saves ds_writebyte,ds_readbyte
// silence: "src/ds1302.c:84: warning 59: function 'readbyte' must return value"
#pragma disable_warning 59

#include "ds1302.h"

#define MAGIC_HI  0x5A
#define MAGIC_LO  0xA5

#define INCR(num, low, high) if (num < high) { num++; } else { num = low; }

/*
  Judge whether need to initialize RAM or not by checking RAM address 0x10-0x11 has A5,5A (MAGIC).
  When MAGIC key is found, initialize. Other case, read out the data.
 */
void ds_ram_config_init() {
    uint8_t i,j;
    // check magic bytes to see if ram has been written before
    if ( (ds_readbyte( DS_CMD_RAM >> 1 | 0x00) != MAGIC_LO || ds_readbyte( DS_CMD_RAM >> 1 | 0x01) != MAGIC_HI) ) {
        // if not, must init ram config to defaults
        ds_writebyte( DS_CMD_RAM >> 1 | 0x00, MAGIC_LO);
        ds_writebyte( DS_CMD_RAM >> 1 | 0x01, MAGIC_HI);

        ds_ram_config_write();	// OPTIMISE : Will generate a ljmp to ds_ram_config_write
        return;
    }
    
    // read ram config
    // OPTIMISE : end condition of loop !=4 will generate less code than <4 
    // OPTIMISE : was cfg_table[i] = ds_readbyte(DS_CMD_RAM >> 1 | (i+2));
    //            using a second variable instead of DS_CMD_RAM>>1 | (i+2) generates less code
    j = DS_CMD_RAM >> 1 | 2;
    for (i = 0; i != 4; i++) {
        cfg_table[i] = ds_readbyte(j);
        j++;
    }
}


/*
  Data written to EEPROM
  0x10 A5
  0x11 5A
  0x12 HH             <- cfg_table[0]
  0x13 MM             <- cfg_table[1]
  0x14 Temperature    <- cfg_table[2]
  0x15 --
 */
void ds_ram_config_write() {
    uint8_t i, j;
    j = DS_CMD_RAM >> 1 | 2;
    for (i=0; i!=4; i++) {
        ds_writebyte( j, cfg_table[i]);
        j++;
    }
}

void sendbyte(uint8_t b)
{
    b;
  __asm
    push	ar7
    mov     a,dpl
    mov	r7,#8
00001$:
    nop
    nop
    rrc     a
    mov     _DS_IO,c
    setb	_DS_SCLK
    nop
    nop
    clr	_DS_SCLK
    djnz	r7,00001$
    pop	ar7
  __endasm;
}

uint8_t readbyte()
{
  __asm
	push	ar7
	mov 	a,#0
	mov 	r7,#8
00002$:
	nop
	nop
	mov	c,_DS_IO
	rrc	a	
	setb	_DS_SCLK
	nop
	nop
	clr	_DS_SCLK
	djnz	r7,00002$
	mov	dpl,a
	pop	ar7
  __endasm;
}

uint8_t ds_readbyte(uint8_t addr) {
    // ds1302 single-byte read
    uint8_t b;
    b = DS_CMD | DS_CMD_CLOCK | addr << 1 | DS_CMD_READ;
    DS_CE = 0;
    DS_SCLK = 0;
    DS_CE = 1;
    // send cmd byte
    sendbyte(b);
    // read byte
    b = readbyte();
    DS_CE = 0;
    return b;
}

void ds_readburst() {
    // ds1302 burst-read 8 bytes into struct
    uint8_t j, b;
    b = DS_CMD | DS_CMD_CLOCK | (DS_BURST_MODE << 1) | DS_CMD_READ;
    DS_CE = 0;
    DS_SCLK = 0;
    DS_CE = 1;
    // send cmd byte
    sendbyte(b);
    // read bytes
    for (j = 0; j != 8; j++) {
        rtc_table[j] = readbyte();
    }
    DS_CE = 0;
}

void ds_writebyte(uint8_t addr, uint8_t data) {
    // ds1302 single-byte write
    uint8_t b = 0;
    b = DS_CMD | DS_CMD_CLOCK | addr << 1 | DS_CMD_WRITE;
    DS_CE = 0;
    DS_SCLK = 0;
    DS_CE = 1;
    // send cmd byte
    sendbyte(b);
    // send data byte
    sendbyte(data);

    DS_CE = 0;
}

/*
  Initialize DS1302
  bit7 of control register is WP (Write Protect) bit. This bit is unstable after power up, therefore need to set 0 clear and can be written. If this bit is set as 1, can't be written to any registers.

  Bit 7 of register for second information is clock halt (CH) flag. When it is 1, stop clock oscilator and enter low power consumption mode under 100nA.
  When it is set 0, start clock. Just after power-up, this bit is unstable, need to initialize. But maintain the origianl information of second.
 */
void ds_init() {
    uint8_t b = ds_readbyte(DS_ADDR_SECONDS);
    ds_writebyte(DS_ADDR_WP, 0); // clear WP
    b &= ~(0b10000000);	// clear Bit7
    ds_writebyte(DS_ADDR_SECONDS, b); // clear CH
}

/*
// reset date, time
void ds_reset_clock() {
    ds_writebyte(DS_ADDR_MINUTES, 0x00);
    ds_writebyte(DS_ADDR_HOUR,  DS_MASK_1224_MODE|0x07);
    ds_writebyte(DS_ADDR_MONTH, 0x01);
    ds_writebyte(DS_ADDR_DAY,   0x01);
}
*/

void ds_hours_12_24_toggle() {

    uint8_t hours,b;
    if (H12_12) { // 12h->24h
        hours = ds_split2int(rtc_table[DS_ADDR_HOUR] & DS_MASK_HOUR12); // hours in 12h format (1-11am 12pm 1-11pm 12am)
        if (hours == 12) {
            if (!H12_PM) {
                hours = 0;
            }
        } else {
            if (H12_PM) {
                hours += 12;			 // to 24h format
            }
        }
        b = ds_int2bcd(hours);			 // clear hour_12_24 bit
    }
    else { // 24h->12h
        hours = ds_split2int(rtc_table[DS_ADDR_HOUR] & DS_MASK_HOUR24); // hours in 24h format (0-23, 0-11=>am , 12-23=>pm)
        b = DS_MASK_1224_MODE;
        if (hours >= 12) { 	// pm
            hours -= 12;
            b |= DS_MASK_PM;
        }
        if (hours == 0) {  		//12am
            hours = 12;
        }
        b |= ds_int2bcd(hours);
    }

    ds_writebyte(DS_ADDR_HOUR, b);
}

// increment hours
void ds_hours_incr() {
    uint8_t hours, b = 0;
    if (!H12_12) {
        hours = ds_split2int(rtc_table[DS_ADDR_HOUR] & DS_MASK_HOUR24);	//24h format
        INCR(hours, 0, 23);
        b = ds_int2bcd(hours);		// bit 7 = 0
    } else {
        hours = ds_split2int(rtc_table[DS_ADDR_HOUR] & DS_MASK_HOUR12);	//12h format
        INCR(hours, 1, 12);
        if (hours == 12) {
            H12_PM = !H12_PM;
        }
        b = ds_int2bcd(hours) | DS_MASK_1224_MODE;
        if (H12_PM) {
            b |=  DS_MASK_PM;
        }
    }
    ds_writebyte(DS_ADDR_HOUR, b);
}

// increment minutes
void ds_minutes_incr() {
    uint8_t minutes = ds_split2int(rtc_table[DS_ADDR_MINUTES] & DS_MASK_MINUTES);
    INCR(minutes, 0, 59);
    ds_writebyte(DS_ADDR_MINUTES, ds_int2bcd(minutes));
}

// increment year
void ds_year_incr() {
    uint8_t year = ds_split2int(rtc_table[DS_ADDR_YEAR] & DS_MASK_YEAR);
    INCR(year, 0, 99);
    ds_writebyte(DS_ADDR_YEAR, ds_int2bcd(year));
}

// increment month
void ds_month_incr() {
    uint8_t month = ds_split2int(rtc_table[DS_ADDR_MONTH] & DS_MASK_MONTH);
    INCR(month, 1, 12);
    ds_writebyte(DS_ADDR_MONTH, ds_int2bcd(month));
}

// increment day
void ds_day_incr() {
    uint8_t day = ds_split2int(rtc_table[DS_ADDR_DAY] & DS_MASK_DAY);
    INCR(day, 1, 31);
    ds_writebyte(DS_ADDR_DAY, ds_int2bcd(day));
}

void ds_alarm_minutes_incr() {
    uint8_t mm = cfg_table[CFG_ALARM_MINUTES_BYTE] & CFG_ALARM_MINUTES_MASK;
    INCR(mm, 0, 59);
    cfg_table[CFG_ALARM_MINUTES_BYTE] &= ~CFG_ALARM_MINUTES_MASK;
    cfg_table[CFG_ALARM_MINUTES_BYTE] |= mm;
    ds_ram_config_write();
}

void ds_alarm_hours_incr() {
    uint8_t hh = cfg_table[CFG_ALARM_HOURS_BYTE] >> CFG_ALARM_HOURS_SHIFT;
    INCR(hh, 0, 23);
    hh <<= CFG_ALARM_HOURS_SHIFT;
    cfg_table[CFG_ALARM_HOURS_BYTE] &= ~CFG_ALARM_HOURS_MASK;
    cfg_table[CFG_ALARM_HOURS_BYTE] |= hh;
    ds_ram_config_write();
}

void ds_alarm_on_toggle() {
    CONF_ALARM_ON = !CONF_ALARM_ON;
    ds_ram_config_write();
}

void ds_chime_since_incr() {
    uint8_t hh = cfg_table[CFG_CHIME_SINCE_BYTE ] >> CFG_CHIME_SINCE_SHIFT;
    INCR(hh, 0, 23);
    hh <<= CFG_CHIME_SINCE_SHIFT;
    cfg_table[CFG_CHIME_SINCE_BYTE] &= ~CFG_CHIME_SINCE_MASK ;
    cfg_table[CFG_CHIME_SINCE_BYTE] |= hh;
    ds_ram_config_write();
}

void ds_chime_until_incr() {
    uint8_t hh = cfg_table[CFG_CHIME_UNTIL_BYTE] & CFG_CHIME_UNTIL_MASK;
    INCR(hh, 0, 23);
    cfg_table[CFG_CHIME_UNTIL_BYTE] &= ~CFG_CHIME_UNTIL_MASK;
    cfg_table[CFG_CHIME_UNTIL_BYTE] |= hh;
    ds_ram_config_write();
}

void ds_chime_on_toggle() {
    CONF_CHIME_ON = !CONF_CHIME_ON;
    ds_ram_config_write();
}

void ds_date_mmdd_toggle() {
    CONF_SW_MMDD = !CONF_SW_MMDD;
    ds_ram_config_write();
}

void ds_temperature_offset_incr() {
    uint8_t offset = cfg_table[CFG_TEMP_BYTE] & CFG_TEMP_MASK;
    offset++;
    offset &= CFG_TEMP_MASK;
    cfg_table[CFG_TEMP_BYTE] = (cfg_table[CFG_TEMP_BYTE] & ~CFG_TEMP_MASK) | offset;
    ds_ram_config_write();
}

void ds_temperature_cf_toggle() {
    CONF_C_F = !CONF_C_F;
    ds_ram_config_write();
}

void ds_weekday_incr() {
    uint8_t day = rtc_table[DS_ADDR_WEEKDAY];
    INCR(day, 1, 7);
    ds_writebyte(DS_ADDR_WEEKDAY, day);
    rtc_table[DS_ADDR_WEEKDAY] = day;		// usefull ?
}

void ds_sec_zero() {
    rtc_table[DS_ADDR_SECONDS] = 0;
    ds_writebyte(DS_ADDR_SECONDS, 0b10000000); // set CH, stop clock
    ds_writebyte(DS_ADDR_SECONDS, 0); // clear CH, start clock
}
    
uint8_t ds_split2int(uint8_t tens_ones) {
    return (tens_ones >> 4) * 10 + (tens_ones & 0x0F);
}

// return bcd byte from integer
uint8_t ds_int2bcd(uint8_t integer) {
    return integer / 10 << 4 | integer % 10;
}

uint8_t ds_int2bcd_tens(uint8_t integer) {
    return integer / 10 % 10;
}

uint8_t ds_int2bcd_ones(uint8_t integer) {
    return integer % 10;
}
