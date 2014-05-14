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
string currentSentData;
std::vector<Client> clients;
Client toAccept;
map<boost::asio::ip::udp::endpoint, int> clientMap;
boost::asio::ip::udp::endpoint udp_received_endpoint;
char udpBuffer[1000000];

int16_t output_buf[100000]; 
int lastUpload = 0;
int lastData = 0;
int lastClient = 0;
int PORT = 14582;// numer portu, z ktorego korzysta serwer do komunikacji (zarówno TCP, jak i UDP), domyślnie 10000 + (numer_albumu % 10000); ustawiany parametrem -p serwera, opcjonalnie też klient (patrz opis)
int FIFO_SIZE = 10560; // rozmiar w bajtach kolejki FIFO, którą serwer utrzymuje dla każdego z klientów; ustawiany parametrem -F serwera, domyślnie 10560
int FIFO_LOW_WATERMARK = 10;// opis w treści; ustawiany parametrem -L serwera, domyślnie 0
int FIFO_HIGH_WATERMARK = 20;// opis w treści; ustawiany parametrem -H serwera, domyślnie równy FIFO_SIZE
int BUF_LEN = 10; // rozmiar (w datagramach) bufora pakietów wychodzących, ustawiany parametrem -X serwera, domyślnie 10 
int RETRANSMIT_LIMIT = 10; // opis w treści; ustawiany parametrem -X klienta, domyślnie 10
int TX_INTERVAL = 5; 

Client::Client()
    :queueState(UNINITIALIZED),
    udpRegistered(false),
    lastPacket(0),
    minFifoSecond(0),
    maxFifoSecond(0),
    ticket(false) { }

int main(int argc, char** argv) {
	po::options_description desc("Allowed options");
	desc.add_options()
		("help", "produce help message")
		("port", po::value<int>(), "set server port")
		("fifo", po::value<int>(), "set FIFO size")
		("low", po::value<int>(), "set low watermark")
		("high", po::value<int>(), "set high watermark")
		("buflen", po::value<int>(), "set buffer length")
		("retransmit", po::value<int>(), "set retransmit limit")
		("txinterval", po::value<int>(), "tx interval (msec)")
	;

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);

	if (vm.count("help")) {
		cout << desc << "\n";
		return 1;
	}

	if (vm.count("port"))
		PORT = vm["port"].as<int>();
	if (vm.count("fifo"))
		FIFO_SIZE = vm["fifo"].as<int>();
	if (vm.count("low"))
		FIFO_LOW_WATERMARK = vm["low"].as<int>();
	if (vm.count("high"))
		FIFO_HIGH_WATERMARK = vm["high"].as<int>();
	if (vm.count("buflen"))
		BUF_LEN = vm["buflen"].as<int>();
	if (vm.count("retransmit"))
		RETRANSMIT_LIMIT = vm["retransmit"].as<int>();
	if (vm.count("txinterval"))
		TX_INTERVAL = vm["txinterval"].as<int>();
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
	udpReceiveNext();
	ioservice.run();
}

void acceptNext() {
	toAccept = Client();
	toAccept.socket = new boost::asio::ip::tcp::socket(ioservice);
	acceptor.async_accept(*toAccept.socket, onAccept);
}

void onAccept(const e_code& error) {
	if (handleError(error, "onAccept"))
		return;
	// disables Nagle's algorithm
	toAccept.socket->set_option(boost::asio::ip::tcp::no_delay(true));
	stringstream welcome;
	welcome << "CLIENT " << clients.size() << "\n";
	shared_ptr<string> welcomeStr(new string(welcome.str()));
	boost::asio::async_write(*toAccept.socket,
	    boost::asio::buffer(*welcomeStr, welcomeStr->length()),
	    [&] (const e_code& error, std::size_t bytes_transferred) {
	    	handleError(error, "onAccept:greeting");
	    });
	toAccept.udpEndpoint = boost::asio::ip::udp::endpoint();
	toAccept.udpEndpoint.address(toAccept.socket->remote_endpoint().address());
	clients.push_back(toAccept);
	acceptNext();
}

void onSigint(
    const boost::system::error_code& error,
    int signal_number) {
	if (handleError(error, "onSigint"))
		return;
	std::cout << "Exiting." << std::endl;
	acceptor.cancel();
	acceptor.close();
	sock_dgram.cancel();
	sock_dgram.close();
	for (auto client: clients) {
		if (client.socket) {
			client.socket->cancel();
			client.socket->close();
		}
	}
	ioservice.stop();	
}

int recognizeClient(boost::asio::ip::udp::endpoint& remote_endpoint) {
	auto v = clientMap.find(remote_endpoint);
	if (v == clientMap.end())
		return -1;
	return v->second;
}

void evalClientUdpCommand(stringstream& stream, boost::asio::ip::udp::endpoint remote_endpoint) {
	int clientId;
	stream >> clientId;
	clientMap[remote_endpoint] = clientId;
	clients[clientId].udpEndpoint = remote_endpoint;
	clients[clientId].queueState = FILLING;
	cerr << "Client registered: " << boost::lexical_cast<std::string>(remote_endpoint) <<
	 " as #" << clientId << endl;
}

void evalUploadUdpCommand(stringstream& stream, int clientId, size_t bytes) {
	if (clientId == -1)
	{
		cerr << "Client is not registered!";
		return;
	}
	clients[clientId].ticket = true;
	int packetId;
	stream >> packetId;
	stream.get();
	for (int i=stream.tellg(); i<bytes-1; i+=2) {
		if (clients[clientId].queue.size() == FIFO_SIZE) 
		{
			cerr << "Client " << clientId << " has its queue filled!" << endl;
		}
		else {
			int16_t v = 0;
			char c1 = udpBuffer[i];
			char c2 = udpBuffer[i+1];
			//cerr << (int)c1 << " " << (int)c2 << endl;
			v = c1 + c2 * 256;
			clients[clientId].queue.push_back(v);
		}
	}
	updateMinMaxFifo(clientId);
	//cerr << "FIFOSIZE" << clientId << " " << clients[clientId].queue.size() << " " << bytes << endl;
	stringstream response;
	response << "ACK " << packetId << " " << (FIFO_SIZE - clients[clientId].queue.size() * 2) << '\n';
	shared_ptr<string> responseStr(new string(response.str()));
	cerr << "POTWIERDZAM " << packetId << endl;
	sock_dgram.async_send_to(
		boost::asio::buffer(*responseStr, responseStr->length()),
		clients[clientId].udpEndpoint,
		[&] (const e_code& error, std::size_t bytes_transferred) {
			cerr << "POTWIERDZONE." << endl;
	    	handleError(error, "upload:sendAcknowledgement");		    
		});
}

void evalKeepaliveUdpCommand(stringstream& stream, int clientId, size_t bytes) {

}

void evalRetransmitUdpCommand(stringstream& stream, int clientId, size_t bytes) {

}

void udpReceiveNext() {
	sock_dgram.async_receive_from(
		boost::asio::buffer(udpBuffer, 10000),
		udp_received_endpoint,
		[] (const e_code& error,
			size_t bytes_transferred) {
			if (bytes_transferred > 0) {
				if (handleError(error, "onUdpReceived"))
					return;
				int clientId = recognizeClient(udp_received_endpoint);

				stringstream stream(udpBuffer);
				string command;
				stream >> command;
				if (command == "CLIENT")
					evalClientUdpCommand(stream, udp_received_endpoint);
				else if (command == "UPLOAD")
					evalUploadUdpCommand(stream, clientId, bytes_transferred);
				else if (command == "KEEPALIVE")
					evalKeepaliveUdpCommand(stream, clientId, bytes_transferred);
				else if (command == "RETRANSMIT")
					evalRetransmitUdpCommand(stream, clientId, bytes_transferred);
				else
					cerr << "Unsupported datagram command: " << command << endl;
				udpReceiveNext();
			}
		});
}


void sendPeriodicState(const e_code& error) {
	if (handleError(error, "sendPeriodicState"))
		return;
	stringstream strstate;
	strstate << '\n';
	for (auto& client: clients) {
		if (client.queueState != ERROR)
			try {
				std::string addr = boost::lexical_cast<std::string>(client.socket->remote_endpoint());
				strstate << addr << " FIFO: " << client.queue.size() << "/" << FIFO_SIZE << 
					" (min. " << client.minFifoSecond << ", max. " << client.maxFifoSecond << ")\n";
				client.minFifoSecond = client.maxFifoSecond = client.queue.size();
			} catch (boost::system::system_error err) {
				setErrorConnectionState(client);
			}
	}
	state = strstate.str();
	for (auto& client: clients) {
		if (client.queueState != ERROR)
			boost::asio::async_write(*client.socket, 
				boost::asio::buffer(state, state.length()), 
				[&] (const e_code& error, std::size_t bytes_transferred) {
	    			handleError(error, "sendPeriodicState");
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
		//in.len = client.queueState == FILLING ? 0 : client.queue.size() * 2;
		in.len = client.queue.size() * 2;
		in.consumed = 0;
		inputs.push_back(in);
	}
	size_t output_size;
	mixer(&inputs[0], inputs.size(), output_buf, &output_size, TX_INTERVAL);
	for (int i=0; i<clients.size(); i++) {
		clients[i].queue.erase(clients[i].queue.begin(), clients[i].queue.begin() + inputs[i].consumed / 2);
		updateMinMaxFifo(i);
	}
	return output_size;
}

void updateMinMaxFifo(int clientId) {
	int sz = clients[clientId].queue.size();
	if (clients[clientId].minFifoSecond == 0)
		clients[clientId].minFifoSecond = sz;
	else
		clients[clientId].minFifoSecond = min(clients[clientId].minFifoSecond, sz);
	clients[clientId].maxFifoSecond = max(clients[clientId].maxFifoSecond, sz);

	if (clients[clientId].queueState == FILLING && 
		clients[clientId].queue.size() >= FIFO_HIGH_WATERMARK)
		clients[clientId].queueState = ACTIVE;
	if (clients[clientId].queueState == ACTIVE && 
		clients[clientId].queue.size() <= FIFO_LOW_WATERMARK)
		clients[clientId].queueState = FILLING;	
}

void setErrorConnectionState(Client& client) {
	client.queueState = ERROR;
	if (client.socket)
		client.socket->close();
	cerr << "Connection error for client." << endl;
}

void transmitData(const e_code&) {
	size_t size = mix();

	for (auto client: clients) {
		if (!client.ticket || client.queueState == ERROR || client.queueState == UNINITIALIZED)	
			continue;
		client.ticket = false;
		stringstream header;
		header << "DATA " << lastData << " " << client.lastPacket << " " << 
			(FIFO_SIZE - client.queue.size() * 2) << '\n';
		header.write((char*)output_buf, size);
		shared_ptr<string> toSend(new string(header.str()));
		sock_dgram.async_send_to(
			boost::asio::buffer(*toSend, toSend->length()),
			client.udpEndpoint,
			[&] (const e_code& error, std::size_t bytes_transferred) {
	    		handleError(error, "transmitData", &client);
			});
	}
	lastData++;
	transmitter.expires_at(transmitter.expires_at() + boost::posix_time::milliseconds(TX_INTERVAL));
	transmitter.async_wait(&transmitData);
}

bool handleError(const e_code& error, const string& caller, Client* client) {
	if (!error)
		return false;
	if (client)
		setErrorConnectionState(*client);
	cerr << "Error: " << error << " in " << caller << endl;
	cerr << error.message() << endl;
	return true;
}
