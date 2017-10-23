#include <cstring>
#include "transceiver.h"
#include <unistd.h>
#include <poll.h>

/**
 * Check whether a file descriptor is ready for reading
 *
 * @param fd, file descriptor to be checked
 * @return boolean indicating whether fd is ready for read
 */
bool poll_read(const int fd) {
	// Checks a file descriptor is ready for reading
	struct pollfd fds[1];
	int timeout = 0;
	fds[0].fd = fd;
	fds[0].events = POLLIN;
	// Check if the file descriptor is ready for reading
	if (poll(fds, 1, timeout))
		return fds[0].revents & POLLIN;
}

/**
 * Check whether a file descriptor is ready for writing
 *
 * @param fd: File descriptor to be checked
 * @return Boolean indicating whether fd is ready for write
 */
bool poll_write(const int fd) {
	// Checks if a file descriptor is available for reading to
	struct pollfd fds[1];
	int timeout = 0;
	fds[0].fd = fd;
	fds[0].events = POLLOUT | POLLHUP;
	if (poll(fds, 1, timeout)) {
		if (fds[0].revents & POLLHUP)
			return false;
		else if (fds[0].revents & POLLOUT)
			return true;
		else
			return false;
	}
}

namespace comms {

	int Transceiver::recvPacket(Packet *p) {
		if (poll_read(_fd_recv)) {
			int n = read(_fd_recv, (void*) p, sizeof (Packet));
			return n;
		}
		return 0;
	}

	int Transceiver::sendPacket(Packet *p) {
		if (poll_write(_fd_send)) {
			int n = write(_fd_send, (void*) p, sizeof (Packet));
			return n;
		}
		return 0;
	}

	int Transceiver::recvBytes(void *data, int max) {
		if (poll_read(_fd_recv)) {
			int n = read(_fd_recv, data, max);
			return n;
		}
		return 0;
	}

	int Transceiver::sendBytes(void* data, int len) {
		if (poll_write(_fd_send)) {
			int n = write(_fd_send, data, len);
			return n;
		}
		return 0;
	}
}