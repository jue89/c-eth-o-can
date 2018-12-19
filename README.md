# Ethernet over CAN

Interconnect devices by a physical CAN connection, but talk Ehternet on the wire.

Required:
 * A device running Linux with a supported UART interface (i.e. you have to access it through a tty character device from userspace) and enable TAP/TUN driver.
 * A CAN transceiver connected to your UART (e.g. MCP2561/2).
 * Some bus wires with two 120 Ohms termination resistors.

How does it work:
 * A TAP device listens for network packets.
 * Once a packet is dropped into the TAP device, the raw packet is brought onto the bus using the TX pin of your UART.
 * Every connected device will receive sent bytes, even the sending device.
 * The sending device compares transmitted and received bytes. If they do not match, a collision occured.
 * Collieded packets will be resent after a random time delay.
 * All receiving devices will forward the packet to the TAP device.
