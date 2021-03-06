/**
 * REXUS PIOneERS - Pi_1
 * RPi_IMU.cpp
 * Purpose: Implementation of functions for controlling the BerryIMU as defined
 *		in RPi_IMU class
 *
 * @author David Amison
 * @version 3.1 10/10/2017
 */

#include "RPi_IMU.h"

#include <stdio.h>
#include <stdint.h>
#include <iostream>
//Includes for multiprocessing
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>  // For waitpid

// For communication and data packing
#include "comms/pipes.h"
#include "comms/transceiver.h"
#include "comms/packet.h"
#include "comms/protocol.h"

#include <string>
#include "timing/timer.h"
#include <fstream>  //For writing to files
#include <sstream>
#include <iomanip>

// Includes for I2c
#include <linux/i2c-dev.h>
#include "LSM9DS1.h"

#include "logger/logger.h"
#include <error.h>

bool RPi_IMU::activateSensor(int addr) {
	if (ioctl(i2c_file, I2C_SLAVE, addr) < 0) {
		Log("ERROR") << "Failed to aquire bus and/or talk to slave";
		return false;
	}
	Log("INFO") << "Selected sensor (" << addr << ")";
	return true;
}

//Functions for setting up the various sensors

bool RPi_IMU::setupAcc(int reg5_value, int reg6_value) {
	Log("INFO") << "Setting up Accelerometer registers.\n\tCTRL_REG5_XL-"
			<< reg5_value << "\n\tCTRL_REG6_XL-" << reg6_value;
	//Write the values to the control registers
	bool a = writeReg(ACC_ADDRESS, CTRL_REG5_XL, reg5_value);
	bool b = writeReg(ACC_ADDRESS, CTRL_REG6_XL, reg6_value);
	if (a && b) {
		Log("INFO") << "Accelerometer setup successfully";
		return true;
	} else {
		Log("ERROR") << "Accelerometer setup failed";
		return false;
	}
}

bool RPi_IMU::setupGyr(int reg1_value, int reg4_value, int reg_orient_value) {
	Log("INFO") << "Setting up Gyro registers.\n\tCTRL_REG1_G-"
			<< reg1_value << "\n\tCTRL_REG4-" << reg4_value
			<< "\n\tORIENT_CFG_G" << reg_orient_value;
	bool a = writeReg(GYR_ADDRESS, CTRL_REG1_G, reg1_value);
	bool b = writeReg(GYR_ADDRESS, CTRL_REG4, reg4_value);
	bool c = writeReg(GYR_ADDRESS, ORIENT_CFG_G, reg_orient_value);
	if (a && b && c) {
		Log("INFO") << "Gyro setup successfully";
		return true;
	} else {
		Log("ERROR") << "Gyro setup failed";
		return false;
	}
}

bool RPi_IMU::setupMag(int reg1_value, int reg2_value, int reg3_value, int reg4_value) {
	Log("INFO") << "Setting up Magnetometer registers.\n\tCTRL_REG1_M-"
			<< reg1_value << "\n\tCTRL_REG2_M-" << reg2_value
			<< "\n\tCTRL_REG3_M-" << reg3_value
			<< "\n\tCTRL_REG4_M-" << reg4_value;
	bool a = writeReg(MAG_ADDRESS, CTRL_REG1_M, reg1_value);
	bool b = writeReg(MAG_ADDRESS, CTRL_REG2_M, reg2_value);
	bool c = writeReg(MAG_ADDRESS, CTRL_REG3_M, reg3_value);
	bool d = writeReg(MAG_ADDRESS, CTRL_REG4_M, reg4_value);
	if (a && b && c && d) {
		Log("INFO") << "Gyro setup successfully";
		return true;
	} else {
		Log("ERROR") << "Gyro setup failed";
		return false;
	}
}

bool RPi_IMU::writeReg(int addr, int reg, int value) {
	if (!_bus_active) {
		Log("ERROR") << "i2c bus not connected";
		return false;
	}
	if (activateSensor(addr)) {
		Log("INFO") << "Writing " << value << " to register " << reg
				<< " on device " << addr;
	} else {
		Log("ERROR") << "Problem selecting device: " << addr;
		return false;
	}

	int result = i2c_smbus_write_byte_data(i2c_file, reg, value);
	if (result == -1) {
		Log("ERROR") << "Failed to write byte to i2c register";
		return false;
	}
	return true;
}

uint16_t RPi_IMU::readAccAxis(int axis) {
	if (!_bus_active) {
		Log("ERROR") << "Bus not active-reading data";
		return 0;
	}
	//Select the device
	if (!activateSensor(ACC_ADDRESS)) {
		Log("ERROR") << "Failed to activate accelerometer to read data";
		return 0;
	}
	//axis is either 1:x, 2:y, 3:z
	int reg1, reg2;
	switch (axis) {
		case 1:
			reg1 = OUT_X_L_XL;
			reg2 = OUT_X_H_XL;
			break;
		case 2:
			reg1 = OUT_Y_L_XL;
			reg2 = OUT_Y_H_XL;
			break;
		case 3:
			reg1 = OUT_Z_L_XL;
			reg2 = OUT_Z_H_XL;
			break;
		default:
			return 0;
	}
	//Read from the registers
	uint8_t Acc_L = i2c_smbus_read_word_data(i2c_file, reg1);
	uint8_t Acc_H = i2c_smbus_read_word_data(i2c_file, reg2);
	uint16_t data = (uint16_t) (Acc_L | Acc_H << 8);

	return data;
}

uint16_t RPi_IMU::readGyrAxis(int axis) {
        if (!_bus_active) {
                Log("ERROR") << "Bus not active-reading data";
                return 0;
        }

	//Select the device
	if (!activateSensor(GYR_ADDRESS)) {
		Log("ERROR") << "Failed to activate gyro to read data";
		return 0;
	}
	//axis is either 1:x, 2:y, 3:z
	int reg1, reg2;
	switch (axis) {
		case 1:
			reg1 = OUT_X_L_G;
			reg2 = OUT_X_H_G;
			break;
		case 2:
			reg1 = OUT_Y_L_G;
			reg2 = OUT_Y_H_G;
			break;
		case 3:
			reg1 = OUT_Z_L_G;
			reg2 = OUT_Z_H_G;
			break;
		default:
			return 0;
	}
	//Read from the registers
	uint8_t Gyr_L = i2c_smbus_read_word_data(i2c_file, reg1);
	uint8_t Gyr_H = i2c_smbus_read_word_data(i2c_file, reg2);
	uint16_t data = (uint16_t) (Gyr_L | Gyr_H << 8);

	return data;
}

uint16_t RPi_IMU::readMagAxis(int axis) {
        if (!_bus_active) {
                Log("ERROR") << "Bus not active-reading data";
                return 0;
        }

	//Select the device
	if (!activateSensor(MAG_ADDRESS)) {
		Log("ERROR") << "Failed to activate magnetometer to read data";
		return 0;
	}
	activateSensor(MAG_ADDRESS);
	//axis is either 1:x, 2:y, 3:z
	int reg1, reg2;
	switch (axis) {
		case 1:
			reg1 = OUT_X_L_M;
			reg2 = OUT_X_H_M;
			break;
		case 2:
			reg1 = OUT_Y_L_M;
			reg2 = OUT_Y_H_M;
			break;
		case 3:
			reg1 = OUT_Z_L_M;
			reg2 = OUT_Z_H_M;
			break;
		default:
			return 0;
	}
	//Read from the registers
	uint8_t Gyr_L = i2c_smbus_read_word_data(i2c_file, reg1);
	uint8_t Gyr_H = i2c_smbus_read_word_data(i2c_file, reg2);
	uint16_t data = (uint16_t) (Gyr_L | Gyr_H << 8);

	return data;
}

void RPi_IMU::readAcc(uint16_t *data) {
	//Select the accelerometer
	activateSensor(ACC_ADDRESS);
	//Request reading from the accelerometer
	uint8_t block[6];
	uint8_t *block_addr = block;
	i2c_smbus_read_i2c_block_data(i2c_file, 0x80 | OUT_X_L_XL, sizeof (block), block_addr);
	//Calculate x, y and z values
	data[0] = (uint16_t) (block[0] | block[1] << 8);
	data[1] = (uint16_t) (block[2] | block[3] << 8);
	data[2] = (uint16_t) (block[4] | block[5] << 8);

	return;
}

void RPi_IMU::readGyr(uint16_t *data) {
	//Select the gyro sensor
	activateSensor(GYR_ADDRESS);
	//Request reading from the gyro
	uint8_t block[6];
	uint8_t *block_addr = block;
	i2c_smbus_read_i2c_block_data(i2c_file, 0x80 | OUT_X_L_G, sizeof (block), block_addr);
	//Calculate x, y and z values
	data[0] = (uint16_t) (block[0] | block[1] << 8);
	data[1] = (uint16_t) (block[2] | block[3] << 8);
	data[2] = (uint16_t) (block[4] | block[5] << 8);

	return;
}

void RPi_IMU::readMag(uint16_t *data) {
	//Select the magnetometer
	activateSensor(MAG_ADDRESS);
	//Request data
	uint8_t block[6];
	uint8_t *block_addr = block;
	i2c_smbus_read_i2c_block_data(i2c_file, 0x80 | OUT_X_L_M, sizeof (block), block_addr);
	data[0] = (uint16_t) (block[0] | block[1] << 8);
	data[1] = (uint16_t) (block[2] | block[3] << 8);
	data[2] = (uint16_t) (block[4] | block[5] << 8);

	return;
}

void RPi_IMU::readRegisters(comms::byte1_t *data) {
        if (!_bus_active) {
                Log("ERROR") << "Bus not active-reading data";
                throw -1;
        }

	// Read all registers for accelerometer, magnetometer and gyroscope
	int n = 0;
	if (!activateSensor(ACC_ADDRESS))
		throw -1;
	i2c_smbus_read_i2c_block_data(i2c_file, 0x80 | OUT_X_L_XL, 6, data);
	activateSensor(GYR_ADDRESS);
	i2c_smbus_read_i2c_block_data(i2c_file, 0x80 | OUT_X_L_G, 6, (data + 6));
	activateSensor(MAG_ADDRESS);
	i2c_smbus_read_i2c_block_data(i2c_file, 0x80 | OUT_X_L_M, 6, (data + 12));
}

void RPi_IMU::resetRegisters() {
	//Set control registers in the IMU to 0
	writeReg(ACC_ADDRESS, CTRL_REG5_XL, 0);
	writeReg(ACC_ADDRESS, CTRL_REG6_XL, 0);

	writeReg(GYR_ADDRESS, CTRL_REG1_G, 0);
	writeReg(GYR_ADDRESS, CTRL_REG4, 0);
	writeReg(GYR_ADDRESS, ORIENT_CFG_G, 0);

	writeReg(MAG_ADDRESS, CTRL_REG1_M, 0);
	writeReg(MAG_ADDRESS, CTRL_REG2_M, 0);
	writeReg(MAG_ADDRESS, CTRL_REG3_M, 0b00000011);
	writeReg(MAG_ADDRESS, CTRL_REG4_M, 0);
}

comms::Pipe RPi_IMU::startDataCollection(char* filename) {
	Log("INFO") << "Starting data collection";
	try {
		_pipes = comms::Pipe();
		Log("INFO") << "Forking processes...";
		if ((_pid = _pipes.Fork()) == 0) {
			// This is the child process and controls data collection
			Log.child_log();
			comms::Packet p1;
			comms::Packet p2;
			comms::byte1_t data[22];
			int intv = 100;
			Timer measurement_time;
			std::string measurement_start = measurement_time.str_datetime();
			// Infinite loop for taking measurements
			Log("INFO") << "Starting loop for taking measurements";
			for (int j = 0;; j++) {
				// Open the file for saving data
				std::ofstream outf;
				std::stringstream unique_file;
				unique_file << filename << "_" << measurement_start << "_"
						<< std::setfill('0') << std::setw(4) << j << ".txt";
				Log("INFO") << "Opening new file for writing data \"" <<
						unique_file.str() << "\"";
				outf.open(unique_file.str());
				// Take 5 measurements i.e. 1 seconds worth of data
				for (int i = 0; i < 100; i++) {
					Timer tmr;
					readRegisters(data);
					int32_t time = measurement_time.elapsed_micro();
					data[21] = (comms::byte1_t)(0xFF & time >> 0);
					data[20] = (comms::byte1_t)(0xFF & time >> 8);
					data[19] = (comms::byte1_t)(0xFF & time >> 16);
					data[18] = (comms::byte1_t)(0xFF & time >> 24);
					outf << data[0] << "," << data[1] << "," <<
							data[2] << "," << data[3] << "," <<
							data[4] << "," << data[5] << "," <<
							data[6] << "," << data[7] << "," <<
							data[8] << "," << data[9] << "," <<
							data[10] << "," << data[11] << "," <<
							data[12] << "," << data[13] << "," <<
							data[14] << "," << data[15] << "," <<
							data[16] << "," << data[17] << "," <<
							data[18] << "," << data[19] << "," <<
							data[20] << "," << data[21] << std::endl;
					comms::byte1_t id1 = ID_DATA1;
					comms::byte1_t id2 = ID_DATA2;
					comms::byte2_t index = (5 * j) + i;
					comms::Protocol::pack(p1, id1, index, data);
					comms::Protocol::pack(p2, id2, index, data + 12);
					Log("DATA (IMU)") << p1;
					Log("DATA (IMU)") << p2;

					if (_pipes.binwrite(&p1, sizeof (p1)) < 0)
						throw -2;
					if (_pipes.binwrite(&p2, sizeof (p2)) < 0)
						throw -2;
					Log("INFO") << "Packets sent to main process";
					while (tmr.elapsed() < intv)
						tmr.sleep_ms(1);
				}
				// Close the current file, ready to start a new one
				outf.close();
			}
		} else if (_pid > 0) {
			// This is the parent process
			return _pipes; // Return the read portion of the pipe
		} else {
			throw -3;
		}
	} catch (int e) {
		// Ignore a broken pipe and exit silently
		switch (e) {
			case -1:
				Log("FATAL") << "Error activating sensor or bus not available\n\t" << std::strerror(errno);
				_pipes.close_pipes();
				break;
			case -2:
				Log("FATAL") << "Failed to read/write to pipe\n\t" << std::strerror(errno);
				resetRegisters();
				_pipes.close_pipes();
				break;
			case -3:
				Log("FATAL") << "Unable to fork process\n\t" << std::strerror(errno);
				_pipes.close_pipes();
				break;
			default:
				Log("FATAL") << "int error not recognized\n\t" << e;
		}
		Log("INFO") << "Shutting down IMU process";
		exit(e);
	} catch (...) {
		Log("FATAL") << "Caught unknown exception\n\t\"" << std::strerror(errno) <<
				"\"";
		Log("INFO") << "Shutting down IMU process";
		resetRegisters();
		_pipes.close_pipes();
		exit(-2);
	}
}

bool RPi_IMU::status() {
	int status_check;
	if (_pid) {
		pid_t result = waitpid(_pid, &status_check, WNOHANG);
		if (result == 0)
			return true;
		else if (result == _pid)
			return false;
		else {
			Log("ERROR") << "Problem with status check\n\t" << std::strerror(errno);
			return false;
		}
	} else
		return false;
}

int RPi_IMU::stopDataCollection() {
	if (_pid) {
		Log("INFO") << "Stopping IMU process (ID:" << _pid << ")";
		bool died = false;
		for (int i = 0; !died && i < 5; i++) {
			int status = 0;
			kill(_pid, SIGTERM);
			sleep(1);
			if (waitpid(_pid, &status, WNOHANG) == _pid) died = true;
		}
		if (died) {
			Log("INFO") << "Camera process terminated by sending SIGTERM signal";
		} else {
			Log("ERROR") << "SIGTERM signal failed, sending SIGKILL";
			kill(_pid, SIGKILL);
		}
		_pid = 0;
		_pipes.close_pipes();
	}
	return 0;
}

