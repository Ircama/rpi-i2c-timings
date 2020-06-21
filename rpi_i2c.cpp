#include <iomanip>
#include <iostream>

#include <bcm_host.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>

#include "absl/flags/flag.h"
#include "absl/time/time.h"

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

struct I2CRegisterSet {
  uint32_t C;
  uint32_t S;
  uint32_t DLEN;
  uint32_t A;
  uint32_t FIFO;
  uint32_t DIV;
  uint32_t DEL;
  uint32_t CLKT;
};

ABSL_FLAG(uint32_t, set_freq_hz, 0, "I2C frequency to set in Hz");
ABSL_FLAG(absl::Duration, set_timeout, absl::Milliseconds(35),
          "I2C clock stretch timeout to set");

ABSL_FLAG(uint16_t, set_div_cdiv, 0, "(Advanced) raw clock divider value");
ABSL_FLAG(uint16_t, set_clkt_tout, 0,
          "(Advanced) I2C clock stretch timeout value");

int main(int argc, char** argv) {
  uint32_t peripheral_addr_base = bcm_host_get_peripheral_address();

  std::cout << "ARM Linux physical address base of peripherals: 0x"
            << std::hex << peripheral_addr_base << std::dec << std::endl;

  int devmem_fd;
  if ((devmem_fd = open("/dev/mem", O_RDWR|O_SYNC)) < 0) {
    perror("Could not open /dev/mem");
    return errno;
  }

  //I2CRegisterSet i2c_1;
  off_t offset = peripheral_addr_base + I2C1_OFFSET;
  std::cout << "ARM Linux physical address of the BSC1 controller: 0x"
            << std::hex << offset << std::dec << std::endl;
  void* addr = mmap(nullptr, sizeof(I2CRegisterSet), PROT_READ|PROT_WRITE,
                    MAP_SHARED, devmem_fd, offset);

  // mmap(2): "After the mmap() call has returned, the file descriptor, fd,
  // can be closed immediately without invalidating the mapping."
  close(devmem_fd);

  if (addr == MAP_FAILED) {
    perror("Could not mmap I2C registers");
    return errno;
  }

  munmap(addr, sizeof(I2CRegisterSet));
  return 0;
}
