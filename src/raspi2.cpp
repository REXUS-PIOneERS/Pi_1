/*
 * Pi 2 is connected to the ImP/IMU via UART, Camera via CSI, Burn Wire Relay
 * via GPIO and Pi 1 via Ethernet and GPIO for LO, SOE and SODS signals.
 *
 * This program controls most of the main logic and for the PIOneERs mission
 * on board REXUS.
 */

#include <stdio.h>
#include <cstdint>
#include <unistd.h> //For sleep
#include <stdlib.h>
#include <iostream>
#include <signal.h>

#include "pins2.h"
#include "camera/camera.h"
#include "UART/UART.h"
#include "Ethernet/Ethernet.h"
#include "comms/pipes.h"
#include "comms/protocol.h"
#include "comms/packet.h"
#include "timing/timer.h"
#include "logger/logger.h"
#include "tests/tests.h"

#include <wiringPi.h>

Logger Log("/Docs/Logs/raspi2");

bool flight_mode = false;

// Global variable for the Camera and IMU
PiCamera Cam;

// Setup for the UART communications
int baud = 230400;
ImP IMP(baud);
comms::Pipe ImP_stream;

// Ethernet communication setup and variables (we are acting as client)
int port_no = 31415; // Random unused port for communication
Raspi2 raspi2(port_no);

/**
 * Checks whether input is activated
 * @param pin: GPIO to be checked
 * @return true or false
 */
bool poll_input(int pin) {
	int count = 0;
	for (int i = 0; i < 5; i++) {
		count += digitalRead(pin);
		delayMicroseconds(200);
	}
	return (count < 3) ? true : false;
}


/**
 *  Checks the status of three input pins (in1, in2 and in3). If in1 and in2 are high
 *  but in3 is low return value will look like: 0b0000 0011
 *  @return Integer where the three LSB represent the status of in1, in2 and in3.
 */
int poll_signals(int in1, int in2, int in3) {
	int rtn = 0;
	if (poll_input(in1))
		rtn += 0b001;
	if (poll_input(in2))
		rtn += 0b010;
	if (poll_input(in3))
		rtn += 0b100;
	return rtn;
}

void signal_handler(int s) {
	Log("FATAL") << "Exiting program after signal " << s;
	if (Cam.status()) {
		Cam.stopVideo();
		Log("INFO") << "Stopping camera process";
	} else {
		Log("ERROR") << "Camera process died prematurely or did not start";
	}

	if (raspi2.status()) {
		raspi2.end();
		Log("INFO") << "Closed Ethernet communication";
	} else {
		Log("ERROR") << "Ethernet process died prematurely or did not start";
	}
	IMP.stopDataCollection();
	digitalWrite(BURNWIRE, 0);
	// TODO copy data to a further backup directory
	Log("INFO") << "Ending program, Pi rebooting";
	system("sudo reboot");
	exit(1); // This was an unexpected end so we will exit with an error!
}

/**
 * Checks the status of all possible child processes and returns as a
 * string.
 */
 std::string status_check() {
	std::string rtn;
	if (raspi2.status())
		rtn += "Eth_u, ";
	else
		rtn += "Eth_d, ";

	if (Cam.status())
		rtn += "Cam_u, ";
	else
		rtn += "Cam_d, ";

	if (IMP.status())
		rtn += "ImP_u";
	else
		rtn += "ImP_d";
	return rtn;
 }

int SODS_SIGNAL() {
	/*
	 * When the 'Start of Data Storage' signal is received recording of IMU data
	 * stops while the camera continues running till power off or storage space is full
	 */
	Log("INFO") << "SODS signal received";
	if (Cam.status()) {
		Log("INFO") << "Camera still running";
	} else {
		Log("ERROR") << "Camera process died prematurely or did not start";
		Log("INFO") << "Trying to restart camera";
		Cam.startVideo("Docs/Video/rexus_video");
	}
	IMP.stopDataCollection();
	digitalWrite(BURNWIRE, 0);
	digitalWrite(BURNWIRE, 0);
	Log("INFO") << "Waiting for power off";
	while (1) {
		Timer::sleep_ms(10000);
		raspi2.sendMsg("Falling");
	}
	// TODO copy data to a further backup directory
	//Log("INFO") << "Ending program, Pi rebooting";
	//system("sudo reboot");
	return 0;
}

int SOE_SIGNAL() {
	/*
	 * When the 'Start of Experiment' signal is received the boom needs to be
	 * deployed and the ImP and IMU to start taking measurements. For boom
	 * deployment is there is no increase in the encoder count or ?? seconds
	 * have passed since start of deployment then it is assumed that either the
	 * boom has reached it's full length or something has gone wrong and the
	 * count of the encoder is sent to ground.
	 */
	Log("INFO") << "SOE signal received";
	raspi2.sendMsg("Received SOE");
	// Setup the ImP and start requesting data
	ImP_stream = IMP.startDataCollection("Docs/Data/Pi2/imu_data");
	Log("INFO") << "Started data collection from ImP";
	comms::Packet p; // Buffer for reading data from the IMU stream
	
	Timer tmr;
	if (flight_mode) {
		// Trigger the burn wire for 10 seconds!
		Log("INFO") << "Triggering burnwire";
		digitalWrite(BURNWIRE, 1);
		raspi2.sendMsg("Burnwire triggered...");
		Log("INFO") << "Burn wire triggered" << std::endl;
		while (tmr.elapsed() < 10000) {
			// Get ImP data
			int n = ImP_stream.binread(&p, sizeof (p));
			if (n > 0) {
				Log("DATA (ImP)") << p;
				raspi2.sendPacket(p);
			}

			n = raspi2.recvPacket(p);
			if (n > 0)
				Log("DATA (PI1)") << p;
			Timer::sleep_ms(10);
		}
		digitalWrite(BURNWIRE, 0);
		Log("INFO") << "Burn wire off after " << tmr.elapsed() << " ms";
		raspi2.sendMsg("Burnwire off");
	}
	Log("INFO") << "Waiting for SODS";
	// Wait for the next signal to continue the program
	bool signal_received = false;
	int counter = 0;
	while (!signal_received) {
		if (counter++ >=300) {
			// Check camera and ImP are running
			counter = 0;
			// Send the general status update message
			raspi2.sendMsg(status_check());
			if (!Cam.status()) {
				Log("ERROR") << "Camera has stopped running...restarting";
				Cam.startVideo("Docs/Video/restart");
			}
			if (!IMP.status()) {
				Log("ERROR") << "ImP has stopped running...restarting";
				IMP.startDataCollection("Docs/Data/Pi2/restart");
			}
		}
		signal_received = (poll_signals(LO, SOE, SODS) & 0b100);
		// Read data from IMU_data_stream and echo it to Ethernet
		int n = ImP_stream.binread(&p, sizeof (p));
		if (n > 0) {
			Log("DATA (ImP)") << p;
			raspi2.sendPacket(p);
		}

		n = raspi2.recvPacket(p);
		if (n > 0)
			Log("DATA (PI1)") << p;
		Timer::sleep_ms(10);
	}
	return SODS_SIGNAL();
}

int LO_SIGNAL() {
	/*
	 * When the 'Lift Off' signal is received from the REXUS Module the cameras
	 * are set to start recording video and we then wait to receive the 'Start
	 * of Experiment' signal (when the nose-cone is ejected)
	 */
	Log("INFO") << "LO signal received";
	raspi2.sendMsg("Recevied LO");
	Cam.startVideo("Docs/Video/rexus_video");
	Log("INFO") << "Camera started recording video";
	// Poll the SOE pin until signal is received
	Log("INFO") << "Waiting for SOE";
	bool signal_received = false;
	int counter = 0;
	while (!signal_received) {
		Timer::sleep_ms(10);
		signal_received = (poll_signals(LO, SOE, SODS) & 0b110);
		// Send a message evert second
		if (counter++ >= 300) {
			counter = 0;
			raspi2.sendMsg(status_check());
			if (!Cam.status()) {
				Log("ERROR") << "Camera not running...restarting";
				Cam.startVideo("Docs/Video/restart");
			}
		}
	}
	return SOE_SIGNAL();
}

int main() {
	/*
	 * This part of the program is run before the Lift-Off. In effect it
	 * continually listens for commands from the ground station and runs any
	 * required tests, regularly reporting status until the LO Signal is
	 * received.
	 */
	signal(SIGINT, signal_handler);
	// Create necessary directories for saving files
	system("mkdir -p Docs/Data/Pi1 Docs/Data/Pi2 Docs/Data/test Docs/Video Docs/Logs");
	Log.start_log();
	Log("INFO") << "Pi2 is alive";
	wiringPiSetup();
	// Setup main signal pins
	pinMode(LO, INPUT);
	pullUpDnControl(LO, PUD_UP);
	pinMode(SOE, INPUT);
	pullUpDnControl(SOE, PUD_UP);
	pinMode(SODS, INPUT);
	pullUpDnControl(SODS, PUD_UP);
	pinMode(ALIVE, OUTPUT);
	Log("INFO") << "Main signal pins setup";
	// Setup pins and check whether we are in flight mode
	pinMode(LAUNCH_MODE, INPUT);
	pullUpDnControl(LAUNCH_MODE, PUD_UP);
	//flight_mode = digitalRead(LAUNCH_MODE);
	flight_mode = false;
	Log("INFO") << (flight_mode ? "flight mode enabled" : "test mode enabled");

	// Setup Burn Wire
	pinMode(BURNWIRE, OUTPUT);

	// Setup server and wait for client
	digitalWrite(ALIVE, 1);
	Log("INFO") << "Waiting for connection from client on port " << port_no;
	try {
		raspi2.run("Docs/Data/Pi1/backup");
		Log("INFO") << "Connection to Pi1 successfil";
		raspi2.sendMsg("Connected to Pi1");
	} catch (EthernetException e) {
		Log("FATAL") << "Unable to connect to pi 1\n\t" << e.what();
		Log("INFO") << "Continuing without Ethernet connection";

	}
	Log("INFO") << "Waiting for LO signal";
	// Check for LO signal.
	std::string msg;
	bool signal_received = false;
	comms::Packet p;
	comms::byte1_t id;
	comms::byte2_t index;
	char data[16];
	int n;
	while (!signal_received) {
		Timer::sleep_ms(10);
		signal_received = (poll_signals(LO, SOE, SODS) & 0b111);
		// TODO Implement communications with Pi 1
		n = raspi2.recvPacket(p);
		if (n > 0) {
			Log("PI1") << p;
			comms::Protocol::unpack(p, id, index, data);
			if (id == 0b11000000) {
				Log("RXSM") << "Received Command: " << data[0];
				switch (data[0]) {
					case 1: // restart
					{
						Log("INFO") << "Rebooting...";
						system("sudo reboot now");
						break;
					}
					case 2: // shutdown
					{
						Log("INFO") << "Shutting down...";
						system("sudo shutdown now");
						break;
					}
					case 3: // Toggle flight mode
					{
						Log("INFO") << "Changing flight mode";
						flight_mode = data[1];
						Log("INFO") << (flight_mode ? "flight mode enabled" : "test mode enabled");
						if (flight_mode)
							raspi2.sendMsg("WARNING Flight mode enabled");
						else
							raspi2.sendMsg("Test mode enabled");
						break;
					}
					case 4: // Run all tests
					{
						Log("INFO") << "Running tests...";
						std::string result = tests::pi2_tests();
						raspi2.sendMsg(result);
						Log("INFO") << "Test results\n\t" << result;
						break;
					}
					case 5:
					{
						Log("INFO") << "Cleaning files";
						if (data[1] == 0) {
							//Clean everything
							system("sudo rm -rf /Docs/Data/Pi1/*.txt");
							system("sudo rm -rf /Docs/Data/Pi2/*.txt");
							system("sudo rm -rf /Docs/Video/*.h264");
							system("sudo rm -rf /Docs/Data/Logs/*.txt");
						} else if (data[1] == 1) {
							//Clean data
							system("sudo rm -rf /Docs/Data/Pi1/*.txt");
							system("sudo rm -rf /Docs/Data/Pi2/*.txt");
						} else if (data[1] == 2) {
							//Clean video
							system("sudo rm -rf /Docs/Video/*.h264");
						} else if (data[1] == 3) {
							//Clean logs
							system("sudo rm -rf /Docs/Data/Logs/*.txt");
						}
						Timer::sleep_ms(5000);
						system("sudo reboot");
						break;
					}
					case 6:
					{
						Log("INFO") << "Rebuilding software";
						system("sudo rm -rf /home/pi/CPP_PIOneERS/bin/raspi2");
						system("sudo rm -rf /home/pi/CPP_PIOneERS/build/*.o");
						system("sudo make ./bin/raspi2 -C /home/pi/CPP_PIOneERS");
						Timer::sleep_ms(20000);
						Log("INFO") << "Project rebuilt... rebooting";
						system("sudo reboot");
						break;
					}
					default:
					{
						raspi2.sendMsg("Not Recognised");
						Log("ERROR") << "Command not recognised" << data[0];
						break;
					}
				}
			}
			//TODO handle incoming commands!
		}
	}
	LO_SIGNAL();
	system("sudo reboot");
	return 0;
}
