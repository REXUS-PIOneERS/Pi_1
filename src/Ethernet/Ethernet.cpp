/**
 * REXUS PIOneERS - Pi_1
 * Ethernet.cpp
 * Purpose: Implementation of functions for the Server and Client classes for
 *		ethernet communication
 *
 * @author David Amison
 * @version 2.0 12/10/2017
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream> //for std::ofstream
#include <iostream>  //for std::endl

#include <string>
#include <fcntl.h>

#include "Ethernet.h"
#include "pipes/pipes.h"

// Functions for setting up as a server

Server::Server(const int port) {
	m_port = port;

	setup();
}

int Server::setup() {
	// Open the socket for Ethernet connection
	m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (m_sockfd < 0)
		throw EthernetException("ERROR: Server Failed to Open Socket");
	bzero((char *) &m_serv_addr, sizeof (m_serv_addr));
	// Setup details for the server address
	m_serv_addr.sin_family = AF_INET;
	m_serv_addr.sin_addr.s_addr = INADDR_ANY;
	m_serv_addr.sin_port = m_port;
	// Bind the socket to given address and port number
	if (bind(m_sockfd, (struct sockaddr *) &m_serv_addr, sizeof (m_serv_addr)) < 0)
		throw EthernetException("ERROR: On binding server");
	// Sets backlog queue to 5 connections and allows socket to listen
	listen(m_sockfd, 5);
	m_clilen = sizeof (m_cli_addr);
	return 0;
}

std::string Server::receive_packet() {
	// Receive a packet of data from the client and ackowledge receipt
	char buf[256];
	int n;
	n = read(m_newsockfd, buf, 255);
	if (n <= 0)
		return "";
	buf[n] = '\0';
	std::string packet(buf);
	return packet;
}

int Server::send_packet(const std::string packet) {
	/*
	 * Sends a data packet to the server. General format is:
	 * [SYNC] [MSGID] [MSGLEN] [DATA] [CRC]
	 * Data uses consistent overhead byte stuffing
	 */
	char buf[256];
	int n = write(m_newsockfd, packet.c_str(), packet.length());
	// Check we managed to send the data
	return (n > 0) ? n : -1;
}

Server::~Server() {
	close(m_newsockfd);
	close(m_sockfd);
}

// Functions for setting up as a client

Client::Client(const int port, const std::string host_name) {
	m_host_name = host_name;
	m_port = port;
	setup();
}

int Client::setup() {
	// Open the socket
	m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (m_sockfd < 0)
		throw EthernetException("ERROR: Client failed to open socket");
	// Look for server host by given name
	m_server = gethostbyname(m_host_name.c_str());
	if (m_server == NULL)
		throw EthernetException("ERROR: No such host");
	bzero((char *) &m_serv_addr, sizeof (m_serv_addr));
	m_serv_addr.sin_family = AF_INET;
	bcopy((char *) m_server->h_addr,
			(char *) &m_serv_addr.sin_addr.s_addr,
			m_server->h_length);
	m_serv_addr.sin_port = m_port;

	return 0;
}

int Client::open_connection() {
	if (connect(m_sockfd, (struct sockaddr *) &m_serv_addr, sizeof (m_serv_addr)) < 0)
		return -1; // Failed to connect!
	return 0;
}

int Client::send_packet(const std::string packet) {
	/*
	 * Sends a data packet to the server. General format is:
	 * [SYNC] [MSGID] [MSGLEN] [DATA] [CRC]
	 * Data uses consistent overhead byte stuffing
	 */
	char buf[256];
	bzero(buf, 256);
	int n = write(m_sockfd, packet.c_str(), packet.length());
	// Check receipt has been acknowledged
	return (n > 0) ? n : -1;
}

std::string Client::receive_packet() {
	// Receives a data packet from the server
	char buf[256];
	int n = read(m_sockfd, buf, 255);
	if (n <= 0)
		return "";
	buf[n] = '\0';
	std::string packet(buf);
	return packet;
}

int Client::close_connection() {
	if (m_sockfd) {
		send_packet("E");
		close(m_sockfd);
		m_pipes.close_pipes();
	}
}

Client::~Client() {
	close_connection();
}

/*
 * Specific functions for the PIOneERS implementation. The client and server
 * both run as a separate process with a pipe for passing data back and forth.
 * The client regularly checks the pipe for data to be sent and does so.
 * Whenever the server receives a request it also checks it's own pipe for data
 * to be transferred to the client and does so.
 *
 * General format of communication:
 * Client initiates communication and sends first data packet
 * Server responds by acknowledging receipt of the packet
 * Client continues to send packets while it has some and the server responds
 *		accordingly
 * Client informs the server it has finished sending packets.
 * Server then sends any packets it has to the client in a similar process.
 * Server informs the client it is finished and both server and client end
 *		the connection
 */

Pipe Client::run(std::string filename) {
	/*
	 * Generate a pipe for transferring the data, fork the process and send
	 * back the required pipes.
	 * The child process stays open periodically checking the pipe for data
	 * packets and sending these to the server as well as receiving packets
	 * in return.
	 */
	try {
		m_pipes = Pipe();
		open_connection();
		if ((m_pid = m_pipes.Fork()) == 0) {
			// This is the child process.
			std::ofstream outf;
			outf.open(filename);
			// Loop for sending and receiving data
			while (1) {
				char buf[256];
				bzero(buf, 256);
				// Send any data we have
				int n = m_pipes.binread(buf, 255);
				if (n == 0)
					continue;
				buf[n] = '\0';
				std::string packet_send(buf);
				fprintf(stdout, "sending packet...\n");
				if (send_packet(packet_send) < 0)
					throw EthernetException("Failed to send data"); // TODO handle the error
				// Loop for receiving packets
				fprintf(stdout, "packet sent, receiving packet...");
				std::string packet_recv = receive_packet();
				if (!packet_recv.empty()) {
					outf << packet_recv << std::endl;
					fprintf(stdout, "%s\n", packet_recv.c_str());
					//pipes.strwrite(packet_recv);
				}
			}
			outf.close();
			m_pipes.close_pipes();
			exit(0);
		} else {
			// Assign the pipes for the main process and close the un-needed ones
			return m_pipes;
		}
	} catch (PipeException e) {
		fprintf(stdout, "Ethernet %s\n", e.what());
		m_pipes.close_pipes();
		exit(0);
	} catch (...) {
		perror("Error with client");
		m_pipes.close_pipes();
		exit(1);
	}
}

Pipe Server::run(std::string filename) {
	// Fork a process to handle server stuff
	try {
		m_pipes = Pipe();
		printf("Waiting for client connection...\n");
		m_newsockfd = accept(m_sockfd, (struct sockaddr*) & m_cli_addr, &m_clilen);
		if (m_newsockfd < 0)
			throw EthernetException("ERROR: on accept");
		printf("Connection established with client...\n"
				"Beginning data sharing...\n");
		if ((m_pid = m_pipes.Fork()) == 0) {
			// This is the child process that handles all the requests
			std::ofstream outf;
			outf.open(filename);
			// Loop for receiving data
			char buf[256];
			while (1) {
				// Try to get data from the Client
				std::string packet_recv = receive_packet();
				if (!packet_recv.empty()) {
					outf << packet_recv << std::endl;
					//pipes.strwrite(packet);
				}
				// Try to send data to the Client
				int n = m_pipes.binread(buf, 255);
				if (n == 0)
					continue;
				buf[n] = '\0';
				std::string packet_send(buf);
				if (send_packet(packet_send) < 0)
					throw EthernetException("Failed to send data"); // TODO Handling this error
			}
			outf.close();
		} else {
			// This is the main parent process
			return m_pipes;
		}
		close(m_newsockfd);
		close(m_sockfd);
		m_pipes.close_pipes();
		exit(0);

	} catch (PipeException e) {
		// Ignore it and exit gracefully
		fprintf(stdout, "Ethernet %s\n", e.what());
		close(m_newsockfd);
		close(m_sockfd);
		m_pipes.close_pipes();
		exit(0);
	} catch (...) {
		perror("ERROR with server");
		close(m_newsockfd);
		close(m_sockfd);
		m_pipes.close_pipes();
		exit(1);
	}
}
