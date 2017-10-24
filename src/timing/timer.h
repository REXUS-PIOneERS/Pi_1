#ifndef TIMER_H
#define TIMER_H

#include <chrono>
#include <thread>
#include <stdint.h>

class Timer {
public:

	Timer() : beg_(clock_::now()) {
	}

	void reset() {
		beg_ = clock_::now();
	}

	int64_t elapsed() const {
		return (int64_t) std::chrono::duration_cast<nanosecs_>(clock_::now() - beg_).count();
	}

	void sleep_ms(int ms) {
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
	}

private:
	typedef std::chrono::high_resolution_clock clock_;
	typedef std::chrono::duration<uint64_t, std::nano> nanosecs_;
	std::chrono::time_point<clock_> beg_;
};

#endif /* TIMER_H */

