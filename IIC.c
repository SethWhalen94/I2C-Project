#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define wb_clk_i    25*1000000                     // Clock runs at 100Khz
#define prescale    (wb_clk_i/(5*100*1000)-1)    // Value to write to prescale register to set clk frequency to 100Khz (see p4 of the IIC manual)

#define PRERlo  (*(volatile unsigned char *)(0x00408000))    // Clock prescale register low byte
#define PRERhi  (*(volatile unsigned char *)(0x00408002))    // Clock prescale register high byte
#define CTR     (*(volatile unsigned char *)(0x00408004))    // Control register
#define TXR     (*(volatile unsigned char *)(0x00408006))    // Transmit register
#define RXR     (*(volatile unsigned char *)(0x00408006))    // Receive register
#define CR      (*(volatile unsigned char *)(0x00408008))    // Command register
#define SR      (*(volatile unsigned char *)(0x00408008))    // Status register

#define NOP     0   // Don't set STA or STO
#define STA     1   // Set STA
#define STO     2   // Set STO
#define ACK     3   // Master Acknowledge for sequential reading
#define NACK    4   // Master NACK for end of read operation

#define EEPROM_ADDR_LOWER  0x50 //0b1010000 // Slave address of lower block
#define EEPROM_ADDR_UPPER  0x54 //0b1010100 // Slave address of lower block
#define ADCDAC_ADDR        0x48 //0b1001000 // Slave address of ADC/DAC

#define RS232_Status      *(volatile unsigned char *)(0x00400040)
#define RS232_TxData      *(volatile unsigned char *)(0x00400042)
#define RS232_RxData      *(volatile unsigned char *)(0x00400042)
#define PI 3141

int Echo = 0;
// Function Prototypes
int Get2HexDigits(char *CheckSumPtr);
int Get4HexDigits(char *CheckSumPtr);
int Get6HexDigits(char *CheckSumPtr);
int Get8HexDigits(char *CheckSumPtr);
void wait_interrupt();
char xtod(int c);
int _getch( void );

/******************************************************************************************************************************
* Functions used to get different amounts of HEX digits
******************************************************************************************************************************/
int Get2HexDigits(char *CheckSumPtr)
{

    register int i;
    
    Echo = 1;
    i = (xtod(_getch()) << 4) | (xtod(_getch())); 
    Echo = 0;
    if(CheckSumPtr)
        *CheckSumPtr += i ;

    return i ;
}

int Get4HexDigits(char *CheckSumPtr)
{
    return (Get2HexDigits(CheckSumPtr) << 8) | (Get2HexDigits(CheckSumPtr));
}

int Get6HexDigits(char *CheckSumPtr)
{
    return (Get4HexDigits(CheckSumPtr) << 8) | (Get2HexDigits(CheckSumPtr));
}

int Get8HexDigits(char *CheckSumPtr)
{
    return (Get4HexDigits(CheckSumPtr) << 16) | (Get4HexDigits(CheckSumPtr));
}


char xtod(int c)
{
    if ((char)(c) <= (char)('9'))
        return c - (char)(0x30);    // 0 - 9 = 0x30 - 0x39 so convert to number by sutracting 0x30
    else if((char)(c) > (char)('F'))    // assume lower case
        return c - (char)(0x57);    // a-f = 0x61-66 so needs to be converted to 0x0A - 0x0F so subtract 0x57
    else
        return c - (char)(0x37);    // A-F = 0x41-46 so needs to be converted to 0x0A - 0x0F so subtract 0x37
}

int _getch( void )
{
    int c ;
    while(((char)(RS232_Status) & (char)(0x01)) != (char)(0x01))    // wait for Rx bit in 6850 serial comms chip status register to be '1'
        ;

    c = (RS232_RxData & (char)(0x7f));                   // read received character, mask off top bit and return as 7 bit ASCII character

    // shall we echo the character? Echo is set to TRUE at reset, but for speed we don't want to echo when downloading code with the 'L' debugger command
    if(Echo)
        _putch(c);

    return c ;
}

int _putch( int c)
{
    while((RS232_Status & (char)(0x02)) != (char)(0x02))    // wait for Tx bit in status register or 6850 serial comms chip to be '1'
        ;

    RS232_TxData = (c & (char)(0x7f));                      // write to the data register to output the character (mask off bit 8 to keep it 7 bit ASCII)
    return c ;                                              // putchar() expects the character to be returned
}

//=================================
// Method to initialize the IIC controller
//=================================
void init_iic(void)
{
    PRERlo = prescale & 0x00FF;   // Prescale clock lower bits
    PRERhi = (prescale & 0xFF00) >> 8;   // Prescale clock lower bits
}

void en_iic(void)
{
    CTR = 0x80;  // 0b1000 0000 Set enable to 1, interrupt to 0
}

//=================================
// Method to wait until TIP bit is 1, end of transmission
//=================================
int ready(void)
{
    // Check TIP bit 1 to see transmission has finished
    while ((SR & 0x02) == 0x02) //0x02 = 0b00000010
    {}
    return 1;
}

//=================================
// Method to wait for acknowledge back from slave
//=================================
void wait_ack(void) // Must be done after every write
{
    int i = 0;
    // Poll ack bit
    while((SR & 0x80) == 0x80){}
}

//=======================================================================
// Method to poll IF bit until there is a valid byte in the RXR register
//=======================================================================
void wait_interrupt()
{
    int i = 0;
    while((SR & 0x1) == 0) {
        // Wait for IF bit to be 1 indicating we have a valid byte in the RXR register
    }
}

//===================================================
// Method to send Write commands to the slave device
//===================================================
void send(int data, int ctl) // Write data
{
    // Wait until device is ready
    ready();

    // Put address or data into TX register
    TXR = data & 0xFF;
    if (ctl == STA)
    {
        // Generate start if needed
        CR = 0x80 + 0x10;   // Start cond and write mode
    }
    else
    {
        // Set WR bit
        CR = 0x10;    // write mode
    }
    // Wait until device is ready
    ready();

    wait_ack(); // Wait for slave to ack

    // Clear IACK bit
    // Generate stop if needed
    if(ctl == STO)
    {
        CR = 0x41;
    }

}

// ======================================================================================
// Method used for page read, set ACK bit to notify slave when we are still wanting data
// ======================================================================================
char page_ack(int ctl)  
{
    int data;

    CR = 0x21;       // Set READ bit (Bit 5), ACK bit = 0, Clear interrupts with IACK = 1 (Bit 0)

    // We need to wait for IF to be 1, meaning there is data in the RXR register
    wait_interrupt();
    data = RXR;       // Get Data from register

    // We are done doing a page read
    if(ctl ==NACK) {
        CR = 0x69;       // Set Stop bit, Read bit, IACK bit, and NACK bit
    }
    return data;
}

//===================================================
// Method to select block of EEPROM
//===================================================
void selectBlock(int addr)
{
    if(addr > 0xFFFF)   // Upper block select, B = 1
    {
        printf("\n----- Sending slave address upper block ---\n");
        send(((EEPROM_ADDR_UPPER<<1) + 0), STA); //Need to put 0 at end of address for a write
    }
    else                // Lower block select, B = 0
    {
        printf("\n----- Sending slave address lower block ---\n");
        send(((EEPROM_ADDR_LOWER<<1) + 0), STA); //Need to put 0 at end of address for a write
    }

    // Send address (bits 15-8)
    send((addr & 0xFF00) >> 8, NOP);
    // Send address (bits 7-0)
    send(addr & 0x00FF, NOP);

    printf("\nEnd of selectBlock\n");
}

//===================================================
// Method to send Write a byte to EEprom
//===================================================
void write_byte(int addr, int data)
{
    // Write slaveaddress with start bit
    selectBlock(addr);
    // Write byte with stop bit
    send(data, STO);

}


//===================================================
// Method to send Write mutiple bytes to EEProm
//===================================================
void write_page(int addr, int size, int data, int blockSelect)
{
    int i = 0;
    int j;
    int sizeBlock0, sizeBlock1;
    int upperData, lowerData, maxWriteSize, writeOperations, remainder;
    // Write slaveaddress with start bit
    selectBlock(addr);

    printf("Inside write page. \nAddress is %#X\nData is %#X\n", addr, data);

    // If address straddles a 64K boundary, we will need to split the page write into 2 operations
    // One for addresses 0x00000 - 0x0FFFF and one for addresses 0x10000 - 0x1FFFF
    if(blockSelect == 2) {

        sizeBlock0 = 0x10000 - addr;        // Get number of bytes in lower 64K bytes memory block (Block 0): = Boundary - start address
        sizeBlock1 = size - sizeBlock0;     // Get number of bytes in upper 64K bytes memory block (Block 1): = data size - sizeBlock0

        printf("\n ---- Block 0 memory values ----\n");
        // Write data to memory Block0
        for(i = 0; i < sizeBlock0 - 1; i++)
        {

                printf("Writing %#02X to lower address %X\n", data + i, addr + i);
            send(data + i, NOP);
        }

        // Write last byte of data to Block 0 with stop, i == sizeBlock0 - 1
        send(data + i, STO);

                printf("Writing %#02X to lower address %X\n", data + i, addr + i);

        upperData = data + i + 1;   // Set data to start where last data left off
        // Assign new address as base address of Block 1
        addr = 0x10000;

        // Select Block 1 for writing now
        selectBlock(addr);

        printf("\n ---- Block 1 memory values ----\n");
        // Write data to  memory Block 1
        for(i = 0; i < sizeBlock1 - 1; i++)
        {
            //if(i%10==0)
           // {
                printf("Writing %#02X to upper address %X\n", upperData + i, addr + i);
           // }
            send(upperData + i, NOP);
        }

                printf("Writing %#02X to upper address %X\n", upperData + i, addr + i);

        // Write last byte of data to Block 0 with stop, i == sizeBlock1 - 1
        send(upperData + i, STO);
    }
    else if(blockSelect == 3) {

        sizeBlock1 = 0x20000 - addr;        // Get number of bytes in lower 64K bytes memory block (Block 0): = Boundary - start address
        sizeBlock0 = size - sizeBlock1;     // Get number of bytes in upper 64K bytes memory block (Block 1): = data size - sizeBlock0

        printf("\n ---- Block 1 memory values ----\n");
        // Write data to memory Block0
        for(i = 0; i < sizeBlock1 - 1; i++)
        {

            printf("Writing %#02X to upper address %X\n", data + i, addr + i);
            send(data + i, NOP);
        }

        // Write last byte of data to Block 0 with stop, i == sizeBlock0 - 1
        send(data + i, STO);
        printf("Writing %#02X to upper address %X\n", data + i, addr + i);
        lowerData = data + i + 1;   // Set data to start where last data left off
        // Assign new address as base address of Block 1
        addr = 0x00000;

        // Select Block 1 for writing now
        selectBlock(addr);

        // printf("\n ---- Block 0 memory values ----\n");
        // Write data to  memory Block 1
        for(i = 0; i < sizeBlock0 - 1; i++)
        {
            printf("Writing %#02X to lower address %X\n", lowerData + i, addr + i);

            send(lowerData + i, NOP);
        }

            printf("Writing %#02X to lower address %X\n", lowerData + i, addr + i);

        // Write last byte of data to Block 0 with stop, i == sizeBlock1 - 1 ###### Are we supposed to send the STOP command on the last Byte??? #################
        send(lowerData + i, STO);
    }
    // Need to select new address every 128 bytes, size > 128
    else if(blockSelect == 4) {
        // printf(" --- Inside BlockSelect == 4 write ----");
        for (i = 0; i < size-1; i++)
        {
            if(i % 0x100 == 0)
            {
                printf("Writing %#02X to address %X\n", data + i, addr);
            }
            if(addr == 0x0ffff)
            {
                // printf("Addr == 0x0ffff\nWriting %#02X to  address %X\n", data + i, addr);
                send((data + i)%0x100, STO);

                selectBlock(0x10000);

                addr = 0x10000;
            }
            else if(addr == 0x1ffff)
            {
                // printf("Addr == 0x1ffff\nWriting %#02X to  address %X\n", data + i, addr);
                send((data + i)%0x100, STO);
                selectBlock(0x00000);
                addr = 0x00000;
            }
            else if((addr + 1)%0x80 == 0 && (addr != 0x0 && addr != 0x10000))
            {
                // printf("Addr modulo 0x7F == 0\nWriting %#02X to  address %X\n", data + i, addr);
                send((data + i)%0x100, STO);
                addr++;
                selectBlock(addr);
            }
            else
            {
                // printf("No address change\nWriting %#02X to  address %X\n", data + i, addr);
                send((data + i)%0x100, NOP);
                addr++;
            }
        }
        send((data + i)%0x100, STO);
        // printf("Finished writing data\n");
        /*
        maxWriteSize = 127;        // 128 bytes
        writeOperations = size/0x7F;
        remainder = size%0x7F;          // Extra bits on top of 128 byte blocks
        sizeBlock1 = 0x20000 - addr;        // Get number of bytes in lower 64K bytes memory block (Block 0): = Boundary - start address
        sizeBlock0 = size - sizeBlock1;     // Get number of bytes in upper 64K bytes memory block (Block 1): = data size - sizeBlock0

        printf("maxwritesize: %#X\nwriteops: %#X\nremainder: %#X\nsize1: %#X\nsize0: %#X\nAddr: %#X\nData: %#X", maxWriteSize, writeOperations,remainder, sizeBlock1, sizeBlock0, addr, data);
        while(writeOperations > 0){
        // Write 128 bytes
            printf("\nWriting block\nAddress is %#X\nwriteOperations is %#X\n", addr, writeOperations);
            for(i = 0; i < maxWriteSize - 1; i++)
            {
                if(addr == 0x0ffff)
                {
                    printf("Addr == 0x0ffff\nWriting %#02X to  address %X\n", data + i, addr);
                    send(data + i, STO);

                    selectBlock(0x10000);

                    addr = 0x10000;
                }
                else if(addr == 0x1ffff)
                {
                    printf("Addr == 0x1ffff\nWriting %#02X to  address %X\n", data + i, addr);
                    send(data + i, STO);

                    selectBlock(0x00000);
                    addr = 0x00000;
                }
                else if(addr%0x7F == 0 && addr > 0)
                {
                    printf("Addr modulo 0x7F == 0\nWriting %#02X to  address %X\n", data + i, addr);
                    send(data + i, STO);
                    printf("Sent data\nStaus register: %#X", SR);
                    addr++;
                    selectBlock(addr);
                    printf("\nBlock selected\nStatus register: %#X", SR);
                }
                else
                {
                    printf("No address change\nWriting %#02X to  address %X\n", data + i, addr);
                    send(data+i, NOP);
                    addr++;
                }
            }
            
            send(data + i, STO);
            printf("Writing final byte\nWriting %#02X to  address %X\n", data + i, addr);
            data = data + i + 1;
            addr++;
            writeOperations--;
            printf("\nEnd of a 128 byte write operation, Write operations left = %d, the next address is %#2X\n", writeOperations, addr);
            if(writeOperations > 0)
                selectBlock(addr);
        }// End of while loop

        //Write the remainder if there is any
        if(remainder > 0) {
            printf(" Inside the remainder bytes statement\n");
            selectBlock(addr);

            for(j = 0; j < remainder -1; j++) {
                printf("Writing %#02X to  address %X\n", data + j, addr);

                if(addr == 0x0ffff)
                {
                    send(data + j, STO);

                    selectBlock(0x10000);

                    addr = 0x10000;
                }
                else if(addr == 0x1ffff)
                {
                    send(data + j, STO);

                    selectBlock(0x00000);
                    addr = 0x00000;
                }
                else if(addr%0x7F == 0 && addr > 0)
                {
                    printf("Addr % 0x7F == 0\nWriting %#02X to  address %X\n", data + j, addr);
                    send(data + j, STO);

                    addr++;
                    selectBlock(addr);
                }
                else
                {
                    send(data+j, NOP);
                    addr++;
                }

            }
            send(data + j, STO);
        }// end of remainder for loop*/
    }// end of blockSelect == 4

    else {
        // Write all but last byte of data
        for(i = 0; i < size - 1; i++)
        {   
            //if(i%10==0)
            //{
                printf("Writing %#02X to address %X\n", data + i, addr + i);
            //}
            send(data + i, NOP);
        }

        // Write last byte of data with stop
        printf("Writing %#02X to address %X\n", data + i, addr + i);
        send(data + size - 1, STO);
    }

}

//===================================================
// Method to read a byte from EEProm
//===================================================
int read_byte(int addr)
{
    int data;

    // Write slaveaddress with start bit
    
    selectBlock(addr);

    if(addr > 0xFFFF)   // Upper block select, B = 1
    {
        send(((EEPROM_ADDR_UPPER<<1) + 1), STA); //Need to put 1 at end of address for a read
    }
    else                // Lower block select, B = 0
    {
        send(((EEPROM_ADDR_LOWER<<1) + 1), STA); //Need to put 1 at end of address for a read
    }

    data = page_ack(NACK);
    
    return data;
}

//===================================================
// Method to read multiple bytes from EEProm
//===================================================
void read_page(int addr, int size, int blockSelect)
{
    int i = 0;
    int j = 0;
    int sizeBlock0, sizeBlock1;
    int data;
    // Write slaveaddress with start bit
    selectBlock(addr);

    // Write slaveaddress with start bit
    if(addr > 0xFFFF)   // Upper block select, B = 1
    {
        send(((EEPROM_ADDR_UPPER<<1) + 1), STA);
    }
    else                // Lower block select, B = 0
    {
        send(((EEPROM_ADDR_LOWER<<1) + 1), STA);
    }

    // If address straddles a 64K boundary, we will need to split the page write into 2 operations
    // One for addresses 0x00000 - 0x0FFFF and one for addresses 0x10000 - 0x1FFFF
    if(blockSelect == 2) {
        sizeBlock0 = 0x010000 - addr;        // Get number of bytes in lower 64K bytes memory block (Block 0): = Boundary - start address
        sizeBlock1 = size - sizeBlock0;     // Get number of bytes in upper 64K bytes memory block (Block 1): = data size - sizeBlock0

        printf("\n ---- Block 0 memory values ----\n");
        // Read data from memory Block0
        for(i = 0; i < sizeBlock0 - 1; i++)
        {
            data = page_ack(ACK);
            printf("\nRead data %#02X from lower address %#X.", data, addr + i);
        }

        // Write last byte of data to Block 0 with stop, i == sizeBlock0 - 1
        data = page_ack(NACK);
        printf("\nRead data %#02X from lower address %#X.\n", data, addr + i);

        // Assign new address as base address of Block 1
        addr = 0x010000;

        // Select Block 1 for writing now
        selectBlock(addr);

        // Send slave address and READ command, and repeated start
        send(((EEPROM_ADDR_UPPER<<1) + 1), STA);

        printf("\n ---- Block 1 memory values ----\n");
        // read data from memory Block 1
        for(j = 0; j < sizeBlock1 - 1; j++)
        {
            data = page_ack(ACK);
            printf("\nRead data %#02X from upper address %#X.", data, addr + j);
        }

        // Write last byte of data to Block 0 with stop
        data = page_ack(NACK);
        printf("\nRead data %#02X from upper address %#X.\n", data, addr + j);
    }
    else if(blockSelect == 3) {
        sizeBlock1 = 0x020000 - addr;        // Get number of bytes in lower 64K bytes memory block (Block 0): = Boundary - start address
        sizeBlock0 = size - sizeBlock1;     // Get number of bytes in upper 64K bytes memory block (Block 1): = data size - sizeBlock0

        printf("\n ---- Block 1 memory values ----\n");
        // Read data from memory Block0
        for(i = 0; i < sizeBlock1 - 1; i++)
        {
            data = page_ack(ACK);
            printf("\nRead data %#02X from upper address %#X.", data, addr + i);
        }

        // Write last byte of data to Block 0 with stop, i == sizeBlock0 - 1
        data = page_ack(NACK);
        printf("\nRead data %#02X from upper address %#X.\n", data, addr + i);

        // Assign new address as base address of Block 1
        addr = 0x000000;

        // Select Block 1 for writing now
        selectBlock(addr);

        // Send slave address and READ command, and repeated start
        send(((EEPROM_ADDR_LOWER<<1) + 1), STA);

        printf("\n ---- Block 0 memory values ----\n");
        // read data from memory Block 1
        for(j = 0; j < sizeBlock0 - 1; j++)
        {
            data = page_ack(ACK);
            printf("\nRead data %#02X from lower address %#X.", data, addr + j);
        }

        // Write last byte of data to Block 0 with stop
        data = page_ack(NACK);
        printf("\nRead data %#02X from lower address %#X.\n", data, addr + j);
    }
    else {
        for(i = 0; i < size - 1; i++)
        {
            data = page_ack(ACK);
            printf("\nRead data 0x%X from address 0x%X.", data, addr + i);
        }
        data = page_ack(NACK);
        printf("\nRead data 0x%X from address 0x%X.", data, addr + i);
    }

}

//===================================================
// Method to display menu for EEProm chip functions
//===================================================
void EEPROM(void)
{
    
    int mode = 0, addr = 0, size = 0, data = 0, valid = 0, i = 0;
    int endAddr, blockSelect;

    while(!valid)
    {
        printf("\nPlease select a mode by entering a number. \n1: Write byte\n2: Write page\n3: Read byte\n4: Read page\n");
        //mode = _getch();
        scanf("%d", &mode);

        if(mode > 0 && mode < 5) valid = 1;
        else
        {
            printf("\nYou selected an invalid option. Please enter a number between 1 and 4.\n\n");
        }
    }

    valid = 0;

    while(!valid)
    {
        valid = 1;

        printf("Please enter the starting address in the Hex format XXXXXX: \n");
        addr = Get6HexDigits(0);

        //Check address is valid
        if( addr < 0x00000 || addr > 0x1FFFF)
        {
            printf("\nAddress out of bounds. Please enter an address below 0x01FFFF\n");
            valid = 0;
            continue;
        }

        if(mode == 2 || mode == 4)
        {
            printf("\nPlease enter the total number of bytes in the Hex format XXXXXX ( Max number of bytes for write = 0x00007F, read = %#X)\n", 0x1FFFF-addr);
            size = Get6HexDigits(0);
        }



        // Check if range is valid (First block is in range 0x00_0000 to 0x00_FFFF, second range is 0x01_0000 to 0x01_FFFF)
        if(mode == 4 && (size > 0x1FFFF))
        {
            printf("\nSize is too large. Please make sure the address plus the size don't exceed 0x01FFFF.\n");
            valid = 0;
            continue;
        }
        else if(mode == 2 && (size > 0x1FFFF) )
        {
            printf("\nSize is too large. Please make sure address + size doesn't exceed 0x01FFFF\n");
            valid = 0;
            continue;
        }
        if((mode == 2 || mode == 4) && size< 0x1)
        {
            printf("\nSize is too small. Please enter a size greater than 0\n");
            valid = 0;
            continue;
        }
    }


    valid = 0;

    while(!valid)
    {
        valid = 1;
        if(mode == 1 || mode == 2)  // If writing data
        {
            printf("\nPlease enter a byte of data to be written: \n");
            data = Get2HexDigits(0);
        }
        if(data < 0x00 || data > 0xFF)
        {
            printf("\nPlease enter a number between 0x00 and 0xFF.\n");
            valid = 0;
            continue;
        }
    }
    printf("\n\n");
    // Write byte
    if(mode == 1)
    {
        printf("Writing %#02X to address %X.\n", data, addr);
        write_byte(addr, data);
        printf("\nSuccessfully written byte.\n");
    }
    else if(mode == 2)
    {
    // If start address < 0x01_0000 AND end address > 0x00_FFFF - We are crossing 64K boundary of EEPROM, we will need to toggle Block select at 0x01_0000
    // else if start address < 0x01_0000 AND end address < 0x01_0000 - We are only writing to the lower 64K bytes of EEPROM, so Block select = 0
    // else if start address > 0x00_FFFF AND end address > 0x00_FFFF - We are only writing to the UPPER 64K bytes of EEPROM, so Block select = 1
        // Need to check if we are writing across a 64K Byte boundary
        
        printf("Writing %#02X to %#X blocks, starting from address %#X.\n", data, size, addr);
        
        endAddr = addr + size;
        if(addr < 0x010000 && endAddr > 0x0FFFF && size < 0x80)           // We are crossing 64K boundary of EEPROM, we will need to toggle Block select at 0x01_0000
            blockSelect = 2;
        else if (addr < 0x010000 && endAddr < 0x010000 && size < 0x80)     // We are only writing to the LOWER 64K bytes of EEPROM, so Block select = 0
            blockSelect = 0;
        else if (addr > 0x0FFFF && endAddr > 0x0FFFF && endAddr < 0x020000 && size < 0x80)     // We are only writing to the UPPER 64K bytes of EEPROM, so Block select = 1
            blockSelect = 1;
        else if(addr > 0x0FFFF && endAddr > 0x1FFFF && size < 0x80)
            blockSelect = 3;
        else if(size >= 0x80)
            blockSelect = 4;
        else
            printf("There was an error when setting the blockSelect variable - ^Line 321^\n");
        write_page(addr, size, data, blockSelect);
        blockSelect = 0;
    }
    else if(mode == 3)
    {
        printf("Reading from address %#X.\n", addr);

        data = read_byte(addr);

        printf("\nRead data %#02X from address %#X.", data, addr);
    }
    else if(mode == 4)
    {
        printf("Reading %#X blocks, starting from address %#X.\n", size, addr);

        endAddr = addr + size;
        if(addr < 0x010000 && endAddr > 0x0FFFF)           // We are crossing 64K boundary of EEPROM, we will need to toggle Block select at 0x01_0000
            blockSelect = 2;
        else if (addr < 0x010000 && endAddr < 0x010000)     // We are only writing to the LOWER 64K bytes of EEPROM, so Block select = 0
            blockSelect = 0;
        else if (addr > 0x0FFFF && endAddr > 0x0FFFF && endAddr < 0x1FFFF)     // We are only writing to the UPPER 64K bytes of EEPROM, so Block select = 1
            blockSelect = 1;
        else if(addr > 0x0FFFF && endAddr > 0x1FFFF)
            blockSelect = 3;
        else
            printf("There was an error when setting the blockSelect variable - ^Line 321^\n");

        read_page(addr, size, blockSelect);
        blockSelect = 0;
        printf("\n");
    }
    return;
}

//===================================================
// Method to display analog data from DAC on LED
//===================================================
void DAC(void)
{
    int i = 0, d = 0, scalar = 100;
    int flip = 0;

    //Send start and slave address, read mode
    send((ADCDAC_ADDR << 1) + 0, STA);

    //Send control byte
    send(0x43, NOP);//0b01000011  0, Enable output, single-ended input,,0, don't increment, channel 3,,

    while (((char)(RS232_Status) & (char)(0x01)) != (char)(0x01)) // Check for any character being pressed
    {
        send(d, NOP);
        i++;
        if(i%scalar == 0)
        {
            if(d>=0xFF) {
                flip = 1;
            }
            else if(d<=0x0){
                flip = 0;
            }

            if(flip) {
                d--;
            }
            else if(!flip){
                d++;
            }

        }
    }
    send(0, STO);

    return;
}

//===================================================
// Method to read sensor data from ADC
//===================================================
void ADC( void )
{
    int count = 0;
    int potent = 0, therm = 0, photo = 0;
    volatile int i;

    //Is control byte needed? Unclear

    //Send start and slave address, read mode
    send((ADCDAC_ADDR << 1) + 0, STA);

    //Send control byte

    // For using the photosensor WORKS
    send(0x46, NOP);//0b0100_0110  AOUT = Enabled(1), AINPUT = 0, Auto-increment=on, A/D channel = 01 (Potentiometer)

    // For reading the potentiometer WORKS 0x41
    //send(0x41, NOP);    //0b0101_0001, AOUT = Enabled(1), AIN = channel 1, Auto-increment=off, A/D channel = 01

    // For reading the thermistor, 0x43
    //send(0x43, NOP);    //0b0101_0001, AOUT = Enabled(1), AIN = channel 0, Auto-increment=off, A/D channel = 10

    //Send start and slave address, read mode
    send((ADCDAC_ADDR << 1) + 1, STA);
    
    //Read analog data
    while (((char)(RS232_Status) & (char)(0x01)) != (char)(0x01)) // Check for any character being pressed
    {
        
        potent = page_ack(NOP);
        photo = page_ack(NOP);
        therm = page_ack(NOP);
        page_ack(NOP); 
        printf("Photo resistor: %d\t Potentiometer: %d\t Thermistor: %d\r", photo, potent, therm);

    }
    page_ack(NACK); // Tell ADC we're done
}

//=======================================================
// Method to allow user to choose ADC/DAC chip functions
//========================================================
void ADCDAC(void)   //Lets users choose ADC mode (read photo resistor) or DAC mode (output to LED)
{
    int mode = 0, valid = 0;
    
    while(!valid)
    {
        valid = 1;
        printf("\nPlease choose a mode.\n1: Read Sensors\n2: Display DAC on LED\n");
        scanf("%d", &mode);
        
        if(mode == 1)
        {
            printf("\nADC mode selected. Press any key to exit.\n");
            ADC();
        }
        else if(mode == 2)
        {
            printf("\nDAC mode selected. Press any key to exit.\n");
            DAC();
        }
        else
        {
            printf("\nYou have entered invalid input.\n");
            valid = 0;
        }
        
    }
}

//=================================
// main method
//=================================
int main(void)
{
    int input = 0;
    Echo = 0;

    init_iic();
    en_iic();

    printf("\n\n\nThis function will allow you to write to an IIC device\n\n");

    while(1)
    {
        input = 0;
        printf("\nPlease select a function:\n1: EEPROM\n2: ADC/DAC\n");
        Echo = 1;
        input = _getch() - (char)('0'); //scanf crashes on second loop
        Echo = 0;
        printf("\n");
        if(input == 1)
        {
            EEPROM();
        }
        else if(input == 2)
        {
            ADCDAC();
        }
        else
        {
            printf("\nYou have entered invalid input. Please enter only one number, either 1 or 2.\n");
        }
    }

    return 0;
}