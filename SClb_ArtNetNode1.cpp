/*
 Name:		SClbHESArtNet.cpp
 Created:	24-Feb-19 11:23:40
 Author:	statuscue
 Editor:	http://hes.od.ua
*/

/*
	Copyright 2019-2019 by Yevhen Mykhailov
	Art-Net(TM) Designed by and Copyright Artistic Licence Holdings Ltd.
*/

#include "SClb_ArtNetNode1.h"


//shared static buffer for sending poll replies
uint8_t SCArtNet1::_reply_buffer[ARTNET_REPLY_SIZE];

SCArtNet1::SCArtNet1(IPAddress address)
{
	initialize(0);
	setLocalIP(address);
	_broadcast_address = INADDR_NONE;
}

SCArtNet1::SCArtNet1(IPAddress address, IPAddress subnet_mask)
{
	initialize(0);
	setLocalIP(address, subnet_mask);
}

SCArtNet1::SCArtNet1(IPAddress address, IPAddress subnet_mask, uint8_t* buffer)
{
	initialize(buffer);
	setLocalIP(address, subnet_mask);

	uint32_t a = (uint32_t)address;
	uint32_t s = (uint32_t)subnet_mask;
	_broadcast_address = IPAddress(a | ~s);
}

SCArtNet1::~SCArtNet1(void)
{
	if (_owns_buffer) {		// if we created this buffer, then free the memory
		free(_packet_buffer);
	}
	if (_using_htp) {
		free(_dmx_buffer_a);
		free(_dmx_buffer_b);
		free(_dmx_buffer_c);
	}
}

void  SCArtNet1::initialize(uint8_t* b) {
	if (b == 0) {
		// create buffer
		_packet_buffer = (uint8_t*)malloc(ARTNET_BUFFER_MAX);
		_owns_buffer = 1;
	}
	else {
		// external buffer.  Size MUST be >=ARTNET_BUFFER_MAX
		_packet_buffer = b;
		_owns_buffer = 0;
	}

	memset(_packet_buffer, 0, ARTNET_BUFFER_MAX);

	_using_htp = 0;
	_dmx_buffer_a = 0;
	_dmx_buffer_b = 0;
	_dmx_buffer_c = 0;

	_dmx_slots = 0;
	_dmx_slots_a = 0;
	_dmx_slots_b = 0;
	_universe = 0;
	_net = 0;
	_sequence = 1;

	_dmx_sender = INADDR_NONE;
	_dmx_sender_b = INADDR_NONE;

	initializePollReply();

	_art_tod_req_callback = 0;
	_art_rdm_callback = 0;
	_art_cmd_callback = 0;
}


uint8_t  SCArtNet1::universe(void) {
	return _universe;
}

void SCArtNet1::setUniverse(uint8_t u) {
	_universe = u;
}

void SCArtNet1::setSubnetUniverse(uint8_t s, uint8_t u) {
	_universe = ((s & 0x0f) << 4) | (u & 0x0f);
}

void SCArtNet1::setUniverseAddress(uint8_t u) {
	if (u != 0x7f) {
		if (u & 0x80) {
			_universe = (_universe & 0xf0) | (u & 0x07);
		}
	}
}

void SCArtNet1::setSubnetAddress(uint8_t u) {
	if (u != 0x7f) {
		if (u & 0x80) {
			_universe = (_universe & 0x0f) | ((u & 0x07) << 4);
		}
	}
}

void SCArtNet1::setNetAddress(uint8_t s) {
	if (s & 0x80) {
		_net = (s & 0x7F);
	}
}

void SCArtNet1::setLocalIP(IPAddress a) {
	_my_address = a;
	// Set IP Node
	_reply_buffer[10] = ((uint32_t)_my_address) & 0xff;      //ip address
	_reply_buffer[11] = ((uint32_t)_my_address) >> 8;
	_reply_buffer[12] = ((uint32_t)_my_address) >> 16;
	_reply_buffer[13] = ((uint32_t)_my_address) >> 24;
	// Set BindIP
	_reply_buffer[207] = ((uint32_t)_my_address) & 0xff;      //ip address
	_reply_buffer[208] = ((uint32_t)_my_address) >> 8;
	_reply_buffer[209] = ((uint32_t)_my_address) >> 16;
	_reply_buffer[210] = ((uint32_t)_my_address) >> 24;
}

void SCArtNet1::setLocalIP(IPAddress a, IPAddress sn) {
	setLocalIP(a);
	// calculate broadcast address
	uint32_t a32 = (uint32_t)a;
	uint32_t s32 = (uint32_t)sn;
	_broadcast_address = IPAddress(a32 | ~s32);
}

void SCArtNet1::enableHTP() {
#if defined(__AVR_ATmega168__) || defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__)
	// not enough memory on these to allocate these buffers
#else
	if (!_using_htp) {
		_dmx_buffer_a = (uint8_t*)malloc(DMX_UNIVERSE_SIZE);
		_dmx_buffer_b = (uint8_t*)malloc(DMX_UNIVERSE_SIZE);
		_dmx_buffer_c = (uint8_t*)malloc(DMX_UNIVERSE_SIZE);
		for (int i = 0; i < DMX_UNIVERSE_SIZE; i++) {
			_dmx_buffer_a[i] = 0;
			_dmx_buffer_b[i] = 0;
			_dmx_buffer_c[i] = 0;
		}
		_using_htp = 1;
	}
#endif
}

int  SCArtNet1::numberOfSlots(void) {
	return _dmx_slots;
}

void SCArtNet1::setNumberOfSlots(int n) {
	_dmx_slots = n;
}

uint8_t SCArtNet1::getSlot(int slot) {
	return _packet_buffer[ARTNET_ADDRESS_OFFSET + slot];
}

uint8_t SCArtNet1::getHTPSlot(int slot) {
	return _dmx_buffer_c[slot - 1];
}

void SCArtNet1::setSlot(int slot, uint8_t value) {
	_packet_buffer[ARTNET_ADDRESS_OFFSET + slot] = value;
}

uint8_t* SCArtNet1::dmxData(void) {
	return &_packet_buffer[ARTNET_ADDRESS_OFFSET + 1];
}

uint8_t* SCArtNet1::replyData(void) {
	return _reply_buffer;
}

char* SCArtNet1::shortName(void) {
	return (char*)&_reply_buffer[26];
}

char* SCArtNet1::longName(void) {
	return (char*)&_reply_buffer[44];
}

void SCArtNet1::setNodeName(const char* name) {
	//strcpy((char*)&_reply_buffer[26], name);
	strcpy(longName(), name);
}

uint8_t SCArtNet1::readDMXPacket(UDP* eUDP) {
	uint16_t opcode = readArtNetPacket(eUDP);
	if (opcode == ARTNET_ART_DMX) {
		return RESULT_DMX_RECEIVED;
	}
	return RESULT_NONE;
}

uint8_t SCArtNet1::readDMXPacketContents(UDP* eUDP, int packetSize) {
	if (packetSize > 0) {
		uint16_t opcode = readArtNetPacketContents(eUDP, packetSize);
		if (opcode == ARTNET_ART_DMX) {
			return RESULT_DMX_RECEIVED;
		}
		if (opcode == ARTNET_ART_POLL) {
			return RESULT_PACKET_COMPLETE;
		}
	}
	return RESULT_NONE;
}

/*
  attempts to read a packet from the supplied UDP object
  returns opcode
  sends ArtPollReply with IPAddress if packet is ArtPoll
  replies directly to sender unless reply_ip != INADDR_NONE allowing specification of broadcast
  only returns ARTNET_ART_DMX if packet contained dmx data for this universe
  Packet size checks that packet is >= expected size to allow zero termination or padding
*/
uint16_t SCArtNet1::readArtNetPacket(UDP* eUDP) {
	uint16_t opcode = ARTNET_NOP;
	int packetSize = eUDP->parsePacket();
	if (packetSize > 0) {
		packetSize = eUDP->read(_packet_buffer, ARTNET_BUFFER_MAX);
		opcode = readArtNetPacketContents(eUDP, packetSize);
	}
	return opcode;
}

uint16_t SCArtNet1::readArtNetPacketContents(UDP* eUDP, int packetSize) {
	if (!_using_htp) {
		_dmx_slots = 0;
		/* Buffer now may not contain dmx data for desired universe.
		After reading the packet into the buffer, check to make sure
		that it is an Art-Net packet and retrieve the opcode that
		tells what kind of message it is.                            */
	}

	uint16_t opcode = parse_header();
	switch (opcode) {
	case ARTNET_ART_DMX:
		opcode = ARTNET_NOP;
		// ignore protocol version [10] hi byte [11] lo byte sequence[12], physical[13]
		if ((_packet_buffer[14] == _universe) && (_packet_buffer[15] == _net)) {
			packetSize -= 18;
			uint16_t slots = _packet_buffer[17];
			slots += _packet_buffer[16] << 8;
			if (packetSize >= slots) {					// double check we got all expected
				opcode = readArtDMX(eUDP, slots, packetSize);      // returns ARTNET_ART_DMX
			}
		}   // matched universe/net
		break;
	case ARTNET_ART_ADDRESS:
		if ((packetSize >= 107) && (_packet_buffer[11] >= 14)) {  //protocol version [10] hi byte [11] lo byte
			opcode = parse_art_address(eUDP);
			send_art_poll_reply(eUDP);
		}
		break;
	case ARTNET_ART_POLL:
		if ((packetSize >= 14) && (_packet_buffer[11] >= 14)) {
			send_art_poll_reply(eUDP);
		}
		break;
	case ARTNET_ART_TOD_REQUEST:
		opcode = ARTNET_NOP;
		if ((packetSize >= 25) && (_packet_buffer[11] >= 14)) {
			opcode = parse_art_tod_request(eUDP);
		}
		break;
	case ARTNET_ART_TOD_CONTROL:
		opcode = ARTNET_NOP;
		if ((packetSize >= 24) && (_packet_buffer[11] >= 14)) {
			opcode = parse_art_tod_control(eUDP);
		}
		break;
	case ARTNET_ART_RDM:
		opcode = ARTNET_NOP;
		if ((packetSize >= 24) && (_packet_buffer[11] >= 14)) {
			opcode = parse_art_rdm(eUDP);
		}
		break;
	case ARTNET_ART_CMD:
		parse_art_cmd(eUDP);
		break;
	}
	return opcode;
}

uint16_t SCArtNet1::readArtDMX(UDP* eUDP, uint16_t slots, int packetSize) {
	uint16_t opcode = ARTNET_NOP;
	if (_using_htp) {
		if ((uint32_t)_dmx_sender == 0) {		//if first sender, remember address
			_dmx_sender = eUDP->remoteIP();
			for (int j = 0; j < DMX_UNIVERSE_SIZE; j++) {
				_dmx_buffer_b[j] = 0;	//insure clear buffer 'b' so cancel merge works properly
			}
		}
		if (_dmx_sender == eUDP->remoteIP()) {
			_dmx_slots_a = slots;
			if (_dmx_slots_a > _dmx_slots_b) {
				_dmx_slots = _dmx_slots_a;
			}
			else {
				_dmx_slots = _dmx_slots_b;
			}
			int di;
			int dc = _dmx_slots;
			int dt = ARTNET_ADDRESS_OFFSET + 1;
			for (di = 0; di < dc; di++) {
				if (di < slots) {								// total slots may be greater than slots in this packet
					_dmx_buffer_a[di] = _packet_buffer[dt + di];
				}
				else {										// don't read beyond end of received slots
					_dmx_buffer_a[di] = 0;						// set remainder to zero	
				}
				if (_dmx_buffer_a[di] > _dmx_buffer_b[di]) {
					_dmx_buffer_c[di] = _dmx_buffer_a[di];
				}
				else {
					_dmx_buffer_c[di] = _dmx_buffer_b[di];
				}
			}
			opcode = ARTNET_ART_DMX;
		}
		else { 												// did not match sender a
			if ((uint32_t)_dmx_sender_b == 0) {		// if 2nd sender, remember address
				_dmx_sender_b = eUDP->remoteIP();
			}
			if (_dmx_sender_b == eUDP->remoteIP()) {
				_dmx_slots_b = slots;
				if (_dmx_slots_a > _dmx_slots_b) {
					_dmx_slots = _dmx_slots_a;
				}
				else {
					_dmx_slots = _dmx_slots_b;
				}
				int di;
				int dc = _dmx_slots;
				int dt = ARTNET_ADDRESS_OFFSET + 1;
				for (di = 0; di < dc; di++) {
					if (di < slots) {								//total slots may be greater than slots in this packet				
						_dmx_buffer_b[di] = _packet_buffer[dt + di];
					}
					else {											//don't read beyond end of received slots	
						_dmx_buffer_b[di] = 0;							//set remainder to zero	
					}
					if (_dmx_buffer_a[di] > _dmx_buffer_b[di]) {
						_dmx_buffer_c[di] = _dmx_buffer_a[di];
					}
					else {
						_dmx_buffer_c[di] = _dmx_buffer_b[di];
					}
				}
				opcode = ARTNET_ART_DMX;
			}  // matched sender b
		}     // did not match sender a
	}
	else {								    // NOTE _using_htp only allow one sender
#if defined ( NO_HTP_IS_SINGLE_SENDER )
		if ((uint32_t)_dmx_sender == 0) {		// if first sender, remember address
			_dmx_sender = eUDP->remoteIP();
		}
		if (_dmx_sender == eUDP->remoteIP()) {
#endif
			_dmx_slots = slots;
			//zero remainder of buffer
			for (int n = packetSize + 18; n < ARTNET_BUFFER_MAX; n++) {
				_packet_buffer[n] = 0;
			}
			opcode = ARTNET_ART_DMX;
#if defined ( NO_HTP_IS_SINGLE_SENDER )
		}	// matched sender
#endif
	}
	return opcode;
}

void SCArtNet1::sendDMX(UDP* eUDP, IPAddress to_ip) {
	strcpy((char*)_packet_buffer, "Art-Net");
	_packet_buffer[8] = 0;        //op code lo-hi
	_packet_buffer[9] = 0x50;
	_packet_buffer[10] = 0;
	_packet_buffer[11] = 14;
	if (_sequence == 0) {
		_sequence = 1;
	}
	else {
		_sequence++;
	}
	_packet_buffer[12] = _sequence;
	_packet_buffer[13] = 0;
	_packet_buffer[14] = _universe;
	_packet_buffer[15] = _net;
	_packet_buffer[16] = _dmx_slots >> 8;
	_packet_buffer[17] = _dmx_slots & 0xFF;
	//assume dmx data has been set

	eUDP->beginPacket(to_ip, ARTNET_PORT);
	eUDP->write(_packet_buffer, 18 + _dmx_slots);
	eUDP->endPacket();
}

/*
  sends ArtDMX packet to UDP object's remoteIP if to_ip is not specified
  ( remoteIP is set when parsePacket() is called )
  includes my_ip as address of this node
*/
void SCArtNet1::send_art_poll_reply(UDP* eUDP) {
	_reply_buffer[18] = _net;
	_reply_buffer[19] = (_universe >> 4) & 0x0f;
	//_reply_buffer[190] = (_universe >> 4) & 0x0f;  // TODO ����� ������ �������� � Uni ������� ����� SubUni � 0 �� 3

	IPAddress a = _broadcast_address;
	if (a == INADDR_NONE) {
		a = eUDP->remoteIP();   // reply directly if no broadcast address is supplied
	}
	eUDP->beginPacket(a, ARTNET_PORT);
	eUDP->write(_reply_buffer, ARTNET_REPLY_SIZE);
	eUDP->endPacket();
}

void SCArtNet1::send_art_tod(UDP* wUDP, uint8_t* todata, uint8_t ucount) {
	if (!(_broadcast_address == INADDR_NONE)) {
		uint8_t _buffer[ARTNET_TOD_PKT_SIZE];
		int i;
		for (i = 0; i < ARTNET_TOD_PKT_SIZE; i++) {
			_buffer[i] = 0;
		}
		strcpy((char*)_buffer, "Art-Net");
		_buffer[8] = 0;			// op code lo-hi
		_buffer[9] = 0x81;
		_buffer[10] = 0;		// Art-Net version
		_buffer[11] = 14;
		_buffer[12] = 1;		// RDM version
		_buffer[13] = 1;		// physical port
		//[14-19] spare
		_buffer[20] = 0;		// bind index root device
		_buffer[21] = _net;		//net same as [15] of art-dmx
		if (ucount == 0) {
			_buffer[22] = 1;	// command response 1= TOD not available
		}
		_buffer[23] = _universe;	//port-address same as [14] of art-dmx
		_buffer[24] = 0;			//total UIDs MSB --only single pkt in this implementation
		_buffer[25] = ucount;		//25 total UIDs LSB
		_buffer[26] = 0;			//26 block count (sequence# for multiple packets)
		_buffer[27] = ucount;		//27 UID count
		uint16_t ulen = 6 * ucount;
		for (i = 0; i < ulen; i++) {
			_buffer[28 + i] = todata[i];
		}

		wUDP->beginPacket(_broadcast_address, ARTNET_PORT);
		wUDP->write(_buffer, ulen + 28);
		wUDP->endPacket();
	}	// broadcast != NULL
}

void SCArtNet1::send_art_rdm(UDP* wUDP, uint8_t* rdmdata, IPAddress toa) {
	uint8_t _buffer[ARTNET_RDM_PKT_SIZE];
	int i;
	for (i = 0; i < ARTNET_RDM_PKT_SIZE; i++) {
		_buffer[i] = 0;
	}
	strcpy((char*)_buffer, "Art-Net");
	_buffer[8] = 0;		// op code lo-hi
	_buffer[9] = 0x83;
	_buffer[10] = 0;		// Art-Net version
	_buffer[11] = 14;
	_buffer[12] = 1;		// RDM version
	//[13-20] spare
	_buffer[20] = 1;		// bind index root device
	_buffer[21] = _net;		//net same as [15] of art-dmx
	_buffer[22] = 0;		// command response 0= process the packet
	_buffer[23] = _universe;	//port-address same as [14] of art-dmx

	uint16_t rlen = rdmdata[2] + 1;
	for (i = 0; i < rlen; i++) {
		_buffer[24 + i] = rdmdata[i + 1];
	}

	wUDP->beginPacket(toa, ARTNET_PORT);
	wUDP->write(_buffer, rlen + 24);
	wUDP->endPacket();
}

void SCArtNet1::setArtTodRequestCallback(ArtNetDataRecvCallback callback) {
	_art_tod_req_callback = callback;
}

void SCArtNet1::setArtRDMCallback(ArtNetDataRecvCallback callback) {
	_art_rdm_callback = callback;
}

void SCArtNet1::setArtCommandCallback(ArtNetDataRecvCallback callback) {
	_art_cmd_callback = callback;
}

uint16_t SCArtNet1::parse_header(void) {
	if (strcmp((const char*)_packet_buffer, "Art-Net") == 0) {
		return _packet_buffer[9] * 256 + _packet_buffer[8];  //opcode lo byte first
	}
	return ARTNET_NOP;
}

/*
  reads an ARTNET_ART_ADDRESS packet
  can set output universe
  can cancel merge which resets address of dmx sender
	 (after first ArtDmx packet, only packets from the same sender are accepted
	 until a cancel merge command is received)
*/
uint16_t SCArtNet1::parse_art_address(UDP* wUDP) {
	setNetAddress(_packet_buffer[12]);
	//[14] to [31] short name <= 18 bytes
	if (_packet_buffer[14] != 0) {
		strcpy(shortName(), (char*)&_packet_buffer[14]);
	}
	//[32] to [95] long name  <= 64 bytes
	if (_packet_buffer[32] != 0) {
		strcpy(longName(), (char*)&_packet_buffer[32]);
	}
	//[96][97][98][99]                  input universe   ch 1 to 4
	//[100][101][102][103]               output universe   ch 1 to 4
	setUniverseAddress(_packet_buffer[100]);
	//[102][103][104][105]                      subnet   ch 1 to 4
	setSubnetAddress(_packet_buffer[104]);
	//[105]                                   reserved
	uint8_t command = _packet_buffer[106]; // command
	switch (command) {
	case 0x01:	//cancel merge: resets ip address used to identify dmx sender
		if (_using_htp) {
			if (_dmx_sender != wUDP->remoteIP()) {
				_dmx_sender = (uint32_t)0;
				for (int k = 0; k < DMX_UNIVERSE_SIZE; k++) {
					_dmx_buffer_a[k] = 0;
				}
			}
			if (_dmx_sender_b != wUDP->remoteIP()) {
				_dmx_sender_b = (uint32_t)0;
				for (int k = 0; k < DMX_UNIVERSE_SIZE; k++) {
					_dmx_buffer_b[k] = 0;
				}
			}
		}
		else {
			_dmx_sender = (uint32_t)0;
			for (int j = 18; j < ARTNET_BUFFER_MAX; j++) {
				_packet_buffer[j] = 0;
			}
		}
		break;
	case 0x90:	//clear buffer
		if (_using_htp) {
			_dmx_sender = (uint32_t)0;
			_dmx_sender_b = (uint32_t)0;
			for (int j = 0; j < DMX_UNIVERSE_SIZE; j++) {
				_dmx_buffer_a[j] = 0;
				_dmx_buffer_b[j] = 0;
			}
			_dmx_slots = 512;
		}
		else {
			_dmx_sender = (uint32_t)0;
			for (int j = 18; j < ARTNET_BUFFER_MAX; j++) {
				_packet_buffer[j] = 0;
			}
			_dmx_slots = 512;
		}
		return ARTNET_ART_DMX;	// return ARTNET_ART_DMX so function calling readPacket
								   // knows there has been a change in levels
		break;
	}
	return ARTNET_ART_ADDRESS;
}

uint16_t SCArtNet1::parse_art_tod_request(UDP* wUDP) {
	if (_art_tod_req_callback != NULL) {
		if (_packet_buffer[21] == _net) {
			if (_packet_buffer[24] == _universe) {	//array[32] of port-address
				uint8_t type = 0;
				_art_tod_req_callback(&type);	//pointer to uint8_t could be array of other params
				return ARTNET_ART_TOD_REQUEST;
			}
		}
	}
	return ARTNET_NOP;
}

uint16_t SCArtNet1::parse_art_tod_control(UDP* wUDP) {
	if (_art_tod_req_callback != NULL) {
		if (_packet_buffer[21] == _net) {
			if (_packet_buffer[23] == _universe) {
				uint8_t type = 1;
				_art_tod_req_callback(&type);	//pointer to uint8_t could be array of other params
				return ARTNET_ART_TOD_CONTROL;
			}
		}
	}
	return ARTNET_NOP;
}

uint16_t SCArtNet1::parse_art_rdm(UDP* wUDP) {
	if (_art_rdm_callback != NULL) {
		if (_packet_buffer[21] == _net) {
			if (_packet_buffer[23] == _universe) {
				_art_rdm_callback(&_packet_buffer[24]);
				return ARTNET_ART_RDM;
			}
		}
	}
	return ARTNET_NOP;
}

void SCArtNet1::parse_art_cmd(UDP* wUDP) {
	if (_art_cmd_callback != NULL) {
		if (_packet_buffer[12] == 0xFF) {			// wildcard mfg ID
			if (_packet_buffer[13] == 0xFF) {
				_art_cmd_callback(&_packet_buffer[16]);
			}
		}
	}
}

void  SCArtNet1::initializePollReply(void) {
	int i;
	for (i = 0; i < ARTNET_REPLY_SIZE; i++) {
		_reply_buffer[i] = 0;
	}
	strcpy((char*)_reply_buffer, "Art-Net");
	_reply_buffer[7] = 0;
	_reply_buffer[8] = 0;        // op code lo-hi
	_reply_buffer[9] = 0x21;
	_reply_buffer[10] = ((uint32_t)_my_address) & 0xff;      //ip address
	_reply_buffer[11] = ((uint32_t)_my_address) >> 8;
	_reply_buffer[12] = ((uint32_t)_my_address) >> 16;
	_reply_buffer[13] = ((uint32_t)_my_address) >> 24;
	_reply_buffer[14] = 0x36;    // port lo first always 0x1936
	_reply_buffer[15] = 0x19;
	_reply_buffer[16] = 0;       // firmware hi-lo
	_reply_buffer[17] = 1;
	_reply_buffer[18] = 0;       // subnet hi-lo
	_reply_buffer[19] = 0;
	_reply_buffer[20] = 0x08;       // oem hi-lo 0x1250
	_reply_buffer[21] = 0xC0;
	_reply_buffer[22] = 0;       // ubea
	_reply_buffer[23] = 0xE2;       // status
	_reply_buffer[24] = 0x0A;    //     ESTA Mfg Code
	_reply_buffer[25] = 0x05;
	strcpy((char*)&_reply_buffer[26], "SCArtNetDMX1");
	strcpy((char*)&_reply_buffer[44], "SCArtNetDMX1");
	_reply_buffer[173] = 4;								// Number of ports
	_reply_buffer[174] = 0xC0;							// Can Output from network (port1)
	_reply_buffer[175] = 0xC0;							// Can Output from network (port2)
	_reply_buffer[176] = 0xC0;							// Can Output from network (port3)
	_reply_buffer[177] = 0xC0;							// Can Output from network (port4)
	_reply_buffer[178] = 0x00;							// Good Input (port1)
	_reply_buffer[179] = 0x00;							// Good Input (port2)
	_reply_buffer[180] = 0x00;							// Good Input (port3)
	_reply_buffer[181] = 0x00;							// Good Input (port4)
	_reply_buffer[182] = 0x82;							// Good Output (port1)
	_reply_buffer[183] = 0x82;							// Good Output (port2)
	_reply_buffer[184] = 0x82;							// Good Output (port3)
	_reply_buffer[185] = 0x82;							// Good Output (port4)
	_reply_buffer[186] = _universe;						// Universe In (port1)
	_reply_buffer[187] = _universe + 1;					// Universe In (port2)
	_reply_buffer[188] = _universe + 2;					// Universe In (port3)
	_reply_buffer[189] = _universe + 3;					// Universe In (port4)
	_reply_buffer[190] = _universe;						// Universe Out (port1)
	_reply_buffer[191] = _universe + 1;					// Universe Out (port2)
	_reply_buffer[192] = _universe + 2;					// Universe Out (port3)
	_reply_buffer[193] = _universe + 3;					// Universe Out (port4)
	_reply_buffer[201] = 0x90;							// MAC Address
	_reply_buffer[202] = 0xA2;
	_reply_buffer[203] = 0xDA;
	_reply_buffer[204] = 0x10;
	_reply_buffer[205] = 0x6C;
	_reply_buffer[206] = 0xA8;
	_reply_buffer[211] = 0x1;							//  BindIndex
	_reply_buffer[212] = 0xD;							//  DHCP
}