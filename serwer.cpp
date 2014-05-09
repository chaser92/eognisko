#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "./serwer.h"

Client::Client()
    :queueState(UNINITIALIZED),
    udpRegistered(false) { }

int main(int argc, char** argv) {
    start(PORT);
}

void start(int port) {
	signals.async_wait(onSigint);
	endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port);
	endpoint_udp = boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port);
	acceptor.open(endpoint.protocol());
	acceptor.bind(endpoint);
	acceptor.listen();
	sock_dgram.open(endpoint_udp.protocol());
	sock_dgram.bind(endpoint_udp);
	acceptNext();
	sendPeriodicState(e_code());
	transmitData(e_code());
	ioservice.run();
}

void acceptNext() {
	toAccept = Client();
	toAccept.socket = new boost::asio::ip::tcp::socket(ioservice);
	acceptor.async_accept(*toAccept.socket, onAccept);

}

void onAccept(const e_code& error) {
	if (!error)
	{
		// disables Nagle's algorithm
		toAccept.socket->set_option(boost::asio::ip::tcp::no_delay(true));
		toAccept.queueState = FILLING;
		stringstream welcome;
		welcome << "CLIENT " << clients.size() << "\n";
		boost::asio::async_write(*toAccept.socket,
		    boost::asio::buffer(welcome.str(), welcome.str().length()),
		    [&] (const e_code& error, std::size_t bytes_transferred) {
		    	
		    });
		toAccept.udpEndpoint = boost::asio::ip::udp::endpoint();
		toAccept.udpEndpoint.address(toAccept.socket->remote_endpoint().address());
		clients.push_back(toAccept);
		udpReceiveNext(clients.size() - 1);
		acceptNext();
	}
}

void onSigint(
    const boost::system::error_code& error,
    int signal_number) {
	if (!error)
	{
		std::cout << "Exiting." << std::endl;
		acceptor.cancel();
		acceptor.close();
		sock_dgram.cancel();
		sock_dgram.close();
		for (auto client: clients) {
			client.socket->cancel();
			client.socket->close();
		}
		ioservice.stop();
	}
}

void onUdpReceived(const e_code&, std::size_t bytes_transferred, 
		int clientId) {
	std::cout << "UDP recv'd" << bytes_transferred << std::endl;
	stringstream stream(udpBuffer);
	string command;
	stream >> command;
	if (command == "CLIENT")
		evalClientUdpCommand(stream, clientId);
	else if (command == "DATA")
		evalDataUdpCommand(stream, clientId);

	else
		cerr << "Unsupported datagram command: " << command << endl;
	udpReceiveNext(clientId);
}

void evalClientUdpCommand(stringstream& stream, int clientId) {
	int assumedClientId;
	stream >> assumedClientId;
	if (assumedClientId == clientId) {
		clients[clientId].udpRegistered = true;
		cerr << "Client registered: " << clientId << endl;
	}
	else
		cerr << "Client is not the one he claims he is!" << endl;
}

void evalDataUdpCommand(stringstream& stream, int clientId) {
	cerr << "Data!" << endl;
}

void udpReceiveNext(int clientId) {
	sock_dgram.async_receive_from(boost::asio::buffer(udpBuffer, 10000),
		clients[clientId].udpEndpoint,
		boost::bind(onUdpReceived, boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred, clientId));
}

void sendPeriodicState(const e_code&) {
	std::cout << "Sending periodic state and datagrams." << std::endl;
	stringstream state;
	state << '\n';
	for (auto& client: clients) {
		try {
			std::string addr = boost::lexical_cast<std::string>(client.socket->remote_endpoint());
			state << addr << " FIFO: " << client.queue.size() << "/" << FIFO_SIZE << 
				" (min. 824, max. 7040)\n";
		} catch (boost::system::system_error err) {
			client.queueState = ERROR;
		}
	}
	auto buf = boost::asio::buffer(state.str(), state.str().length());
	for (auto& client: clients) {
		if (client.queueState != ERROR)
			boost::asio::async_write(*client.socket, buf, 
				[&] (const e_code& error, std::size_t bytes_transferred) {
		    		
		   		});
	}
	periodicSender.expires_at(periodicSender.expires_at() + boost::posix_time::seconds(1));
	periodicSender.async_wait(&sendPeriodicState);
}

void transmitData(const e_code&) {
	transmitter.expires_at(transmitter.expires_at() + boost::posix_time::milliseconds(5));
	transmitter.async_wait(&transmitData);

	stringstream header;
	for (auto client: clients) {
		if (client.queueState == ERROR)	
			continue;

	}

}

void onTcpWriteCompleted() {

}