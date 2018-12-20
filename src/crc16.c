#include "crc16.h"

uint16_t crc16Update (uint16_t crc, char octet) {
	crc = (uint8_t)(crc >> 8) | (crc << 8);
	crc ^= octet;
	crc ^= (uint8_t)(crc & 0xff) >> 4;
	crc ^= (crc << 8) << 4;
	crc ^= ((crc & 0xff) << 4) << 1;
	return crc;
}
