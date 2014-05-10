#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "./serwer.h"

boost::asio::io_service ioservice;
boost::asio::ip::tcp::acceptor acceptor(ioservice); 
boost::asio::deadline_timer periodicSender(ioservice, boost::posix_time::seconds(1));
boost::asio::deadline_timer transmitter(ioservice, boost::posix_time::milliseconds(5));
boost::asio::signal_set signals(ioservice, SIGINT, SIGTERM);
boost::asio::ip::tcp::endpoint endpoint;
boost::asio::ip::udp::endpoint endpoint_udp;
boost::asio::ip::udp::socket sock_dgram(ioservice);
string state;
std::vector<Client> clients;
char udpBuffer[10000];
Client toAccept;

int16_t output_buf[20000]; 
int lastUpload = 0;
int lastData = 0;
int lastClient = 0;
int PORT = 14582;// numer portu, z ktorego korzysta serwer do komunikacji (zarówno TCP, jak i UDP), domyślnie 10000 + (numer_albumu % 10000); ustawiany parametrem -p serwera, opcjonalnie też klient (patrz opis)
int FIFO_SIZE = 10560; // rozmiar w bajtach kolejki FIFO, którą serwer utrzymuje dla każdego z klientów; ustawiany parametrem -F serwera, domyślnie 10560
int FIFO_LOW_WATERMARK = 0;// opis w treści; ustawiany parametrem -L serwera, domyślnie 0
int FIFO_HIGH_WATERMARK = FIFO_SIZE;// opis w treści; ustawiany parametrem -H serwera, domyślnie równy FIFO_SIZE
int BUF_LEN = 10; // rozmiar (w datagramach) bufora pakietów wychodzących, ustawiany parametrem -X serwera, domyślnie 10 
int RETRANSMIT_LIMIT = 10; // opis w treści; ustawiany parametrem -X klienta, domyślnie 10
int TX_INTERVAL = 5; 

Client::Client()
    :queueState(UNINITIALIZED),
    udpRegistered(false),
    lastPacket(0) { }

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

void onUdpReceived(const e_code&, size_t bytes_transferred, 
		int clientId) {
	std::cout << "UDP recv'd" << bytes_transferred << std::endl;
	stringstream stream(udpBuffer);
	string command;
	stream >> command;
	if (command == "CLIENT")
		evalClientUdpCommand(stream, clientId, bytes_transferred);
	else if (command == "UPLOAD")
		evalUploadUdpCommand(stream, clientId, bytes_transferred);
	else if (command == "KEEPALIVE")
		evalKeepaliveUdpCommand(stream, clientId, bytes_transferred);
	else
		cerr << "Unsupported datagram command: " << command << endl;
	udpReceiveNext(clientId);
}

void evalClientUdpCommand(stringstream& stream, int clientId, size_t) {
	int assumedClientId;
	stream >> assumedClientId;
	if (assumedClientId == clientId) {
		clients[clientId].udpRegistered = true;
		cerr << "Client registered: " << clientId << endl;
	}
	else
		cerr << "Client is not the one he claims he is!" << endl;
}

void evalUploadUdpCommand(stringstream& stream, int clientId, size_t bytes) {
	if (!clients[clientId].udpRegistered)
	{
		cerr << "Client " << clientId << " is not registered!";
		return;
	}
	int packetId;
	stream >> packetId;
	stream.get();
	for (int i=stream.tellg(); i<bytes-1; i+=2) {
		if (clients[clientId].queue.size() == FIFO_SIZE) 
		{
			cerr << "Client " << clientId << " has its queue filled!" << endl;
			return;
		}
		int32_t v = 0;
		v += stream.get();
		v *= 256;
		v += stream.get();
		clients[clientId].queue.push_back(v);
		cerr << clientId << " Pushin' " << v << endl;
	}
	stringstream response;
	response << "ACK " << packetId << '\n';
	sock_dgram.async_send_to(
		boost::asio::buffer(response.str(), response.str().length()),
		clients[clientId].udpEndpoint,
		[&] (const e_code& error, std::size_t bytes_transferred) {
		    
		});
}

void evalKeepaliveUdpCommand(stringstream& stream, int clientId, size_t bytes) {

}

void udpReceiveNext(int clientId) {
	sock_dgram.async_receive_from(boost::asio::buffer(udpBuffer, 10000),
		clients[clientId].udpEndpoint,
		boost::bind(onUdpReceived, boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred, clientId));
}

void sendPeriodicState(const e_code&) {
	std::cout << "Sending periodic state and datagrams." << std::endl;
	stringstream strstate;
	strstate << '\n';
	for (auto& client: clients) {
		try {
			std::string addr = boost::lexical_cast<std::string>(client.socket->remote_endpoint());
			strstate << addr << " FIFO: " << client.queue.size() << "/" << FIFO_SIZE << 
				" (min. 824, max. 7040)\n";
		} catch (boost::system::system_error err) {
			client.queueState = ERROR;
		}
	}
	state = strstate.str();
	auto buf = boost::asio::buffer(state, state.length());
	for (auto& client: clients) {
		if (client.queueState != ERROR)
			boost::asio::async_write(*client.socket, buf, 
				[&] (const e_code& error, std::size_t bytes_transferred) {
		    		
		   		});
	}
	periodicSender.expires_at(periodicSender.expires_at() + boost::posix_time::seconds(1));
	periodicSender.async_wait(&sendPeriodicState);
}

size_t mix() {
	vector<mixer_input> inputs;
	for (auto client: clients) {
		mixer_input in;
		in.data = (void*)&(*client.queue.begin());
		in.len = client.queue.size();
		in.consumed = 0;
		inputs.push_back(in);
	}
	size_t output_size;
	mixer(&inputs[0], inputs.size(), output_buf, &output_size, TX_INTERVAL);
	for (int i=0; i<clients.size(); i++) {
		clients[i].queue.erase(clients[i].queue.begin(), clients[i].queue.begin() + min(clients[i].queue.size(), inputs[i].consumed));
	}
	return output_size;
}

void transmitData(const e_code&) {
	size_t size = mix();
	transmitter.expires_at(transmitter.expires_at() + boost::posix_time::milliseconds(5));
	transmitter.async_wait(&transmitData);

	for (auto client: clients) {
		if (client.queueState == ERROR)	
			continue;
		stringstream header;
		header << "DATA " << lastData << " " << client.lastPacket << " " << 
			(FIFO_SIZE - client.queue.size()) << '\n';
		header.write((char*)output_buf, size * sizeof(int16_t));
		client.buf_dgram = header.str();
		sock_dgram.async_send_to(
			boost::asio::buffer(client.buf_dgram, client.buf_dgram.length()),
			client.udpEndpoint,
			[&] (const e_code& error, std::size_t bytes_transferred) {
			    
			});
	}
	lastData++;

}

void onTcpWriteCompleted() {

}