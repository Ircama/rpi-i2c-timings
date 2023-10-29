CXX = gcc
CXX_FLAGS = -Wall -Wextra -O3

rpi-i2c: rpi_i2c.c
	$(CXX) $(CXX_FLAGS) rpi_i2c.c -lbcm_host -o rpi-i2c

.PHONY: rpi-i2c
