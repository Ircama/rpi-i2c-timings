#include <bcm_host.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  ((byte) & 0x80 ? '1' : '0'), \
  ((byte) & 0x40 ? '1' : '0'), \
  ((byte) & 0x20 ? '1' : '0'), \
  ((byte) & 0x10 ? '1' : '0'), \
  ((byte) & 0x08 ? '1' : '0'), \
  ((byte) & 0x04 ? '1' : '0'), \
  ((byte) & 0x02 ? '1' : '0'), \
  ((byte) & 0x01 ? '1' : '0') 

#ifndef max
#define max(a,b)   (((a) > (b)) ? (a) : (b))
#endif

// Register layoout is defined in the BCM2711 ARM Peripherals Manual, section 3.2.
//
// The manual lists 0x7E804000 as the address for the BSC1 bus (I2C1). This is
// a *bus* address; the ARM mapping MMU maps it to the ARM *physical* address,
// as seen via /dev/mem.
//
// For instance, on the Raspberry Pi 4B the bus adress offset 0x7E000000 is
// mapped to ARM physical address base 0xFE000000:
// https://github.com/raspberrypi/linux/blob/17cba8a/arch/arm/boot/dts/bcm2711-rpi-4-b.dts#L46
#define I2C1_OFFSET 0x00804000

typedef struct I2CRegisterSetStruct {
  uint32_t C;
  uint32_t S;
  uint32_t DLEN;
  uint32_t A;
  uint32_t FIFO;
  uint32_t DIV;
  uint32_t DEL;
  uint32_t CLKT;
} I2CRegisterSet;

const char core_clk_debugfs_path[] = "/sys/kernel/debug/clk/vpu/clk_rate";

uint32_t print_core_clock_speed(uint16_t cdiv) {
  FILE* clk_fh;
  if ((clk_fh = fopen(core_clk_debugfs_path, "r")) == NULL) {
    printf("Could not open VPU core clock DebugFS path\n");
    return(0);
  }
  uint32_t core_clk_rate;
  if (fscanf(clk_fh, "%u", &core_clk_rate) != 1) {
    printf("Could not read VPU core clock\n");
    return(0);
  }

  if (fclose(clk_fh) != 0) {
    printf("Could not close VPU core clock DebugFS path\n");
    return(0);
  }

  printf("Core clock (MHz): %u\n", core_clk_rate / 1000000);
  printf("I2C clock (KHz): %u\n", core_clk_rate/cdiv / 1000);
  
  return(core_clk_rate/cdiv);
}

int main(int argc, char** argv) {
  if (argc != 1 && argc != 3 && argc != 5) {
    printf("Usage: rpi-i2c [<div.cdiv> <clkt.tout>] [<FEDL> <REDL>]\\n");
    printf("Raspberry Pi I2C timing utility\n\n");
    printf("To read current timing values, run the program without arguments.\n");
    printf("To set new timing values: %s <div.cdiv> <clkt.tout>  [<FEDL> <REDL>]\n\n", argv[0]);
    exit(-1);
  }

  uint16_t cdiv = 0, tout = 0;
  int set_new = 0;
  char* endptr;
  long cdiv_l = -1;
  long fedl_l = -1;
  long redl_l = -1;
  if ((argc == 3) || (argc == 5)) {
    cdiv_l = strtol(argv[1], &endptr, 10);
    if (cdiv_l == LONG_MIN || cdiv_l == LONG_MAX) {
      perror("Could not parse CDIV value");
      return errno;
    }
    if (cdiv_l <= 0 || cdiv_l > 0xFFFF) {
      printf("CDIV out of bounds (0, 65535)\n");
      return ERANGE;
    }
    // CDIV is always rounded down to an even number.
    cdiv = 0xFFFE & cdiv_l;

    long tout_l = strtol(argv[2], &endptr, 10);
    if (tout_l == LONG_MIN || tout_l == LONG_MAX) {
      perror("Could not parse TOUT value");
      return errno;
    }
    if (tout_l < 0 || tout_l > 0xFFFF) {
      printf("TOUT out of bounds (0, 65535)\n");
      return ERANGE;
    }
    tout = 0xFFFF & tout_l;

    set_new = 1;
  }

  if (argc == 5) {
    fedl_l = strtol(argv[3], &endptr, 10);
    if (fedl_l == LONG_MIN || fedl_l == LONG_MAX) {
      perror("Could not parse FEDL value");
      return errno;
    }
    if (fedl_l >= cdiv_l/2) {
      printf("FEDL = %ld out of bounds (0, %ld)\n", fedl_l, cdiv_l/2);
      return ERANGE;
    }
    if (fedl_l <= 0) fedl_l = -fedl_l;

    redl_l = strtol(argv[4], &endptr, 10);
    if (redl_l == LONG_MIN || redl_l == LONG_MAX) {
      perror("Could not parse REDL value");
      return errno;
    }
    if (redl_l >= cdiv_l/2) {
      printf("REDL = %ld  out of bounds (0, %ld)\n", fedl_l, cdiv_l/2);
      return ERANGE;
    }
    if (redl_l <= 0) redl_l = -redl_l;
  }

  uint32_t peripheral_addr_base = bcm_host_get_peripheral_address();
  printf("Raspberry Model type: 0x%x, Processor ID: 0x%x\n",
         bcm_host_get_model_type(), bcm_host_get_processor_id());
  printf("ARM peripheral address base: %#010x\n",
         peripheral_addr_base);
  printf("bcm_host_get_peripheral_size: %#010x\n",
         bcm_host_get_peripheral_size());
  printf("bcm_host_get_sdram_address: %#010x\n",
         bcm_host_get_sdram_address());

  int devmem_fd;
  if ((devmem_fd = open("/dev/mem", O_RDWR|O_SYNC)) < 0) {
    perror("Could not open /dev/mem");
    return errno;
  }

  off_t offset = peripheral_addr_base + I2C1_OFFSET;
  printf("I2C1 controller address base: %#010x\n",
         (uint32_t)offset);

  I2CRegisterSet* i2c_1 = (I2CRegisterSet*)(mmap(
    NULL,
    sizeof(I2CRegisterSet),
    PROT_READ|PROT_WRITE,
    MAP_SHARED,
    devmem_fd,
    offset)
  );

  uint32_t core_clk_rate = print_core_clock_speed(
    cdiv_l > 0 && cdiv? cdiv : i2c_1->DIV);

  // mmap(2): "After the mmap() call has returned, the file descriptor, fd,
  // can be closed immediately without invalidating the mapping."
  close(devmem_fd);

  if (i2c_1 == MAP_FAILED) {
    perror("Could not mmap I2C registers");
    return errno;
  }

  // Sanity check: delay values should not exceed CDIV/2.
  uint16_t FEDL = (i2c_1->DEL >> 16) & 0xFFFF;
  uint16_t REDL = i2c_1->DEL & 0xFFFF;

  printf("C: "BYTE_TO_BINARY_PATTERN" "BYTE_TO_BINARY_PATTERN"\n",
    BYTE_TO_BINARY(i2c_1->C>>8), BYTE_TO_BINARY(i2c_1->C));
  printf("S: "BYTE_TO_BINARY_PATTERN" "BYTE_TO_BINARY_PATTERN"\n",
    BYTE_TO_BINARY(i2c_1->S>>8), BYTE_TO_BINARY(i2c_1->S));
  
  printf("DLEN: %u\n", i2c_1->DLEN);
  printf("A: %u\n", i2c_1->A);
  printf("FIFO: %u\n", i2c_1->FIFO);
  printf("DIV: %u\n", i2c_1->DIV);
  printf("DEL: %u\n", i2c_1->DEL);
  printf("  FEDL: %u\n", FEDL);
  printf("  REDL: %u\n", REDL);
  printf("CLKT: %u\n", i2c_1->CLKT);

  // CDIV and TOUT use only the lower halves of the 32-bit registers.
  printf("DIV.CDIV: %u\n", i2c_1->DIV & 0xFFFF);
  printf("CLKT.TOUT: %u\n", i2c_1->CLKT & 0xFFFF);

  if (set_new) {
    // FEDL & REDL calculation as per the i2c-bcm2835 driver code.
    FEDL = max(cdiv / 16, 1u);
    REDL = max(cdiv / 4, 1u);
    printf("Suggested values: FEDL=%u, REDL=%u. Max: %lu\n",
        FEDL, REDL, cdiv_l/2 - 1);

    if (fedl_l >= 0) {
        FEDL = fedl_l;
    }

    if (redl_l >= 0) {
        REDL = redl_l;
    }

    printf("Updating delay values to: DEL.FEDL=%u = %u microsec. output, DEL.REDL=%u = %u microsec. incoming.\n",
        FEDL, FEDL * 1000000 / core_clk_rate, REDL, REDL * 1000000 / core_clk_rate);

    i2c_1->DIV = (uint32_t)cdiv & 0x0000FFFF;
    i2c_1->CLKT = (uint32_t)tout & 0x0000FFFF;
    i2c_1->DEL = (uint32_t)(((uint32_t)FEDL << 16) | REDL);
    printf("Timing values updated: DIV.CDIV=%u, CLKT.TOUT=%u.\n", cdiv, tout);

    printf("Clock stretching timeout: (microseconds): %u.\n",
        tout * 1000000 / core_clk_rate);
  }

  munmap(i2c_1, sizeof(I2CRegisterSet));
  return 0;
}
