/**
 * REXUS PIOneERS - Pi_1
 * UART.cpp
 * Purpose: Function implementations for the UART class
 *
 * @author David Amison
 * @version 3.2 12/10/2017
 */
#include "UART.h"

#include <stdio.h>
#include <unistd.h>  //Used for UART
#include <fcntl.h>  //Used for UART
#include <termios.h> //Used for UART
// For multiprocessing
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>

#include <fstream>
#include <string>
#include "UART.h"
#include "comms/packet.h"
#include "comms/pipes.h"
#include "comms/transceiver.h"
#include "comms/protocol.h"

#include "timing/timer.h"

#include "logger/logger.h";
#include <error.h>

void UART::setupUART() {
	log << "INFO: Setting up UART";
	//Open the UART in non-blocking read/write mode
	uart_filestream = open("/dev/serial0", O_RDWR | O_NOCTTY);
	if (uart_filestream == -1) {
		log << "FATAL: Unable to open serial port";
		throw UARTException("ERROR opening serial port");
	}
	//Configure the UART
	struct termios options;
	tcgetattr(uart_filestream, &options);
	options.c_cflag = B230400 | CS8 | CLOCAL | CREAD;
	options.c_iflag = IGNPAR;
	options.c_oflag = 0;
	options.c_lflag = 0;
	tcflush(uart_filestream, TCIFLUSH);
	tcsetattr(uart_filestream, TCSANOW, &options);
}

comms::Pipe UART::startDataCollection(const std::string filename) {
	/*
	 * Sends request to the ImP to begin sending data. Returns the file stream
	 * to the main program and continually writes the data to this stream.
	 */
	log << "INFO: Starting ImP and IMU data collection";
	try {
		m_pipes = comms::Pipe();
		if ((m_pid = m_pipes.Fork()) == 0) {
			// This is the child process
			// Infinite loop for data collection
			comms::Transceiver ImP_comms(uart_filestream);
			// Send initial start command
			ImP_comms.sendBytes("C", 1);
			log << "DATA (SENT): C";
			for (int j = 0;; j++) {
				std::ofstream outf;
				char unique_file[50];
				sprintf(unique_file, "%s%04d.txt", filename.c_str(), j);
				log << "INFO: Starting new data file \"" << unique_file << "\"";
				outf.open(unique_file);
				// Take five measurements then change the file
				int intv = 200;
				for (int i = 0; i < 5; i++) {
					Timer tmr;
					char buf[256];
					// Wait for data to come through
					while (1) {
						int n = ImP_comms.recvBytes(buf, 255);
						/*
						 * It is assumed that the data is received in the following format
						 * Byte 1-6 Accelerometer (x, y, z)
						 * Byte 7-12 Gyroscope (x, y, z)
						 * Byte 13-18 Magnetometer (x, y, z)
						 * Byte 19-22 time
						 * Byte 23-24 ImP Measurement
						 */
						if (n > 0) {
							comms::Packet p1;
							comms::Packet p2;
							comms::byte1_t id1 = 0b00100000;
							comms::byte1_t id2 = 0b00100010;
							comms::byte2_t index = (5 * j) + i;
							comms::Protocol::pack(p1, id1, index, buf);
							comms::Protocol::pack(p2, id2, index, buf + 12);
							m_pipes.binwrite(&p1, sizeof (p1));
							m_pipes.binwrite(&p2, sizeof (p2));
							buf[n] = '\0';
							for (int i = 0; i < n; i++)
								outf << (int) buf[n] << ",";
							outf << std::endl;
							log << "DATA (ImP): " << p1;
							log << "DATA (ImP): " << p2;
							ImP_comms.sendBytes("N", 1);
							log << "DATA (SENT): N";
							break;
						}
					}
					while (tmr.elapsed() < intv)
						tmr.sleep_ms(10);
				}
			}
		} else {
			return m_pipes;
		}
	} catch (comms::PipeException e) {
		log << "FATAL: Unable to read/write to pipes\n\t\"" << e.what() << "\"";
		m_pipes.close_pipes();
		close(uart_filestream);
		exit(-1);
	} catch (...) {
		log << "FATAL: Unexpected error with ImP\n\t\"" << std::strerror(errno);
		m_pipes.close_pipes();
		close(uart_filestream);
		exit(-2);
	}
}

int UART::stopDataCollection() {
	log << "INFO: Ending data collection by closing pipes";
	if (m_pid)
		m_pipes.close_pipes();
	return 0;
}
