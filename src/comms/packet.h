#ifndef _PACKET
#define _PACKET

#include <cstdint>



namespace comms {
	typedef uint8_t byte1_t;
	typedef uint16_t byte2_t;

	byte2_t crc_poly = 0x724E;

	int ID_MSG1 = 0b10010000; // Message from Pi 1
	int ID_MSG2 = 0b10100000; // Message from Pi 2
	int ID_STATUS1 = 0b01010000; // Status from Pi 1
	int ID_STATUS2 = 0b01100000; // Status from Pi 2
	int ID_DATA1 = 0b00010000; // Acc/Gyr from Pi 1
	int ID_DATA2 = 0b00010001; // Mag/Time from Pi 1
	int ID_DATA3 = 0b00100000; // Acc/Gyr from Pi 2
	int ID_DATA4 = 0b00100010; //Mag/Time from Pi 2

	/*Make sure this structure is tightly arranged in memory.
	 * Structure is:
	 * byte 1: Sync Byte (0)
	 * byte 2: Overhead Byte (for COBS)
	 * byte 3: ID
	 * byte 4-5: Index of packet
	 * byte 6-21: Data
	 * byte 22-23: Checksum
	 * byte 24: End of Packet (0)
	 */
#pragma pack(push, 1)

	struct Packet {
		byte1_t sync;
		byte1_t ohb;
		byte1_t ID;
		byte2_t index;
		byte1_t data[16];
		byte2_t checksum;
		byte1_t eop;
	};

#pragma pack(pop)

	std::ostream& operator<<(std::ostream &o, Packet &p) {
		o << "ID: " << p.ID << " Index: " << p.index << " Data: ";
		if (p.ID == 0xc0) {
			for (int i = 0; i < 16; i++)
				o << (char) p.data[i];
		} else {
			for (int i = 0; i < 16; i++)
				o << p.data[i];
		}
		o << p.checksum;

		return o;
	}

	/**
		Try to pack up the data.
		@params
		p: address of packet for data to be packed into.
		id: id of the packet
		index: index of the packet
		p_data: starting position of data buffer.
		@return
		0: Success.
		-1: invalid id.
	 */
	int pack(Packet &p, byte1_t id, byte2_t index, byte1_t* p_data) {
		size_t actual_len;
		//id invalid
		if (!(actual_len = lengthByID(id)))
			return -1;
		p.sync = 0x00;
		p.ID = id;
		p.index = index;

		memcpy(p.data, p_data, actual_len);
		memset(p.data + actual_len, '\0', sizeof (p.data) - actual_len);

		//CRC
		p.checksum = Protocol::crc16Gen(&(p.ID), 19, crc_poly);
		//COBS
		Protocol::cobsEncode(&(p.ohb), 23, p.sync);


		return 0;
	}

	/**
	   Try to unpack a packet.
	   @params
	   p: packet to be unpacked
	   id: the reference of ID.
	   index: the reference packet index.
	   p_data: starting position of data buffer.
	   len: the actual length of buffer. Depends on ID
	   @return
	   0: Success.
	   -1: COBS decode failure.
	   -2: CRC mismatch.
	 */
	static int unpack(Packet &p, byte1_t& id, byte2_t& index, byte1_t* p_data) {
		//COBS decode failure
		if (!Protocol::cobsDecode(&(p.ohb), 23, p.sync))
			return -1;

		//CRC mismatch
		if (Protocol::crc16Gen(&(p.ID), 19, crc_poly) != p.checksum)
			return -2;

		id = p.ID;
		index = p.index;
		memcpy(p_data, p.data, sizeof (p.data));

		return 0;
	}

	/**
	   Determine the length of actual data in a packet with id
	   @params
	   id: packet ID
	   @return
	   Length of actual data in bytes. Return 0 if id invalid.
	 */
	static size_t lengthByID(byte1_t id) {
		//Status or message
		if (id & 0b11000000)
			return 16;
			//Measured data
		else {
			switch (id & 0b00111111) {
					//acc/gyr from either IMUs
				case 0b00010000:
				case 0b00100000:
					return 12;
					//mag/time from IMU_1
				case 0b00010001:
					return 10;
					//mag/imp/time from IMU_2
				case 0x00100001:
					return 12;
					//Invalid id
				default:
					return 0;
			}
		}
	}
}

#endif