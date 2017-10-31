/**
 * REXUS PIOneERS - Pi_1
 * camera.cpp
 * Purpose: Implementation of functions in the PiCamera class
 *
 * @author David Amison
 * @version 2.0 10/10/2017
 */

#include <string>
#include <stdint.h>
#include <fstream>
#include "logger.h"
#include "timing/timer.h"

std::string time(Timer tmr) {
	std::stringstream ss;
	uint64_t time = tmr.elapsed();
	int hr = time / 3600000;
	time -= hr * 3600000;
	int min = time / 60000;
	time -= min * 60000;
	int sec = time / 1000;
	time -= sec * 1000;
	ss << hr << ":" << min << ":" << sec << ":" << time;
	return ss.str();
}

namespace log {

	Logger::Logger(std::string filename) {
		_filename = filename;
	}

	void Logger::start_log() {
		_tmr.reset();
		std::ostringstream ss;
		ss << _filename << ".txt";
		std::string _this_filename = ss.str();
		_outf.open(_this_filename);
	}

	std::ofstream& Logger::operator()(std::string str) {
		return _outf << std::endl << str << "(" << time(_tmr) << "): ";
	}

	void Logger::stop_log() {
		_outf.close();
	}

}