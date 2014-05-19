#include "./serwer.h"

// usługa ASIO
boost::asio::io_service ioservice; 

// serwer TCP
boost::asio::ip::tcp::acceptor acceptor(ioservice); 

// do obsługi SIGINT
boost::asio::signal_set signals(ioservice, SIGINT, SIGTERM);

// endpointy serwerów TCP i UDP i klienta UDP
boost::asio::ip::tcp::endpoint endpoint;
boost::asio::ip::udp::endpoint endpoint_udp;

// socket do obsługi i nadawania UDP
boost::asio::ip::udp::socket socketDatagram(ioservice);

// timer do okresowego wysyłania stanu
boost::asio::deadline_timer periodicSender(ioservice, boost::posix_time::milliseconds(1));

// timer do okresowego wysyłania danych audio
boost::asio::deadline_timer transmitter(ioservice, boost::posix_time::milliseconds(1));

// sprawdza, czy klienci są w kontakcie z serwerem
boost::asio::deadline_timer connectivityWatchdog(ioservice, boost::posix_time::milliseconds(50));
string currentSentData;

// zawiera informacje o podłączonych klientach
std::vector<Client> clients;

// zawiera dane klienta, który właśnie się łączy przez TCP
Client toAccept;

// zawiera informacje o endpoincie, z którego nadszedł ostatni datagram
udp_endpoint udpReceivedEndpoint;

// bufor do odbierania danych UDP
char receivedUdpBuffer[1000000];

// bufor do miksowania danych
int16_t mixedOutputBuffer[1000000]; 

// numer ostatnio wysłanego pakietu DATA
int lastData = 0;

// numer ostatnio połączonego klienta
int lastClient = 0; 

// numer portu, z ktorego korzysta serwer do komunikacji (zarowno TCP, jak i UDP)
int PORT = 14582; 

// rozmiar w bajtach kolejki FIFO, ktora serwer utrzymuje dla kazdego z klientow;
int FIFO_SIZE = 10560; 

// ustawiany parametrem -L serwera, domyslnie 0
int FIFO_LOW_WATERMARK = 0; 

// stawiany parametrem -H serwera, domyslnie rowny FIFO_SIZE
int FIFO_HIGH_WATERMARK = -1; 

// rozmiar (w datagramach) bufora pakietow wychodzacych, ustawiany parametrem -X serwera, domyslnie 10 
int BUF_LEN = 10;

// opis w tresci; ustawiany parametrem -X klienta, domyslnie 10
int RETRANSMIT_LIMIT = 10; 

// interwał wysyłania kolejnych datagramów
int TX_INTERVAL = 5; 

Client::Client() 
	: queueState(TCP_ONLY),
    lastPacket(0),
    minFifoSecond(0),
    maxFifoSecond(0)
	{ }

int main(int argc, char** argv) {
	po::options_description desc("Allowed options");
	desc.add_options()
		("help,h", "produce help message")
		("port,p", po::value<int>(), "set server port")
		("fifo,F", po::value<int>(), "set FIFO size")
		("low,L", po::value<int>(), "set low watermark")
		("high,H", po::value<int>(), "set high watermark")
		("buflen,X", po::value<int>(), "set buffer length")
		("txinterval,i", po::value<int>(), "tx interval (msec)");

	po::variables_map vm;
	try {
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);
	} catch (po::unknown_option err) {
		cerr << err.what() << endl;
		return 2;
	} catch (po::invalid_command_line_syntax err) {
		cerr << err.what() << endl;
		return 2;		
	}

	if (vm.count("help")) {
		cout << desc << "\n";
		return 0;
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
	if (vm.count("txinterval"))
		TX_INTERVAL = vm["txinterval"].as<int>();

	if (FIFO_HIGH_WATERMARK == -1)
		FIFO_HIGH_WATERMARK = FIFO_SIZE;
	start(PORT);
}

void start(int port) {
	try {
		signals.async_wait(onSigint);
		endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port);
		endpoint_udp = boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port);
		acceptor.open(endpoint.protocol());
		acceptor.bind(endpoint);
		acceptor.listen();
		socketDatagram.open(endpoint_udp.protocol());
		socketDatagram.bind(endpoint_udp);
		acceptNext();
		sendPeriodicState(e_code());
		transmitData(e_code());
		udpReceiveNext();
		watchdogElapsed();
		ioservice.run();
	} catch (exception const& exc) {
		cerr << exc.what() << endl;
		cerr << "Could not initialize server." << endl;
		deinit();
		return;
	}
}

unsigned long timeMillis() {
	return std::chrono::system_clock::now().time_since_epoch() / 
    	std::chrono::milliseconds(1);
}

void watchdogElapsed(const e_code&) {
	connectivityWatchdog.expires_at(connectivityWatchdog.expires_at() + boost::posix_time::milliseconds(50));
	connectivityWatchdog.async_wait(&watchdogElapsed);	
	unsigned long now = timeMillis();
	for (auto& client : clients) {
		if (client.queueState == TCP_ONLY || client.queueState == ERROR)
			continue;
		if (client.lastContactTime < now - 1000)
			setErrorConnectionState(client);
	}
}

void deinit() {
	std::cout << "Exiting." << std::endl;
	acceptor.cancel();
	acceptor.close();
	try {
		socketDatagram.cancel();
		socketDatagram.close();
		for (auto& client: clients) {
			if (client.socket) {
				client.socket->close();
			}
		}
	} catch (exception ex) { }
	ioservice.stop();	
}

void acceptNext() {
	toAccept = Client();
	toAccept.id = lastClient++;
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
	clients.push_back(toAccept);
	acceptNext();
}

void onSigint(
    const boost::system::error_code& error,
    int signal_number) {
	if (handleError(error, "onSigint"))
		return;
	deinit();
}

Client* recognizeClient(boost::asio::ip::udp::endpoint& remote_endpoint) {
	for (auto& client: clients) 
		if (client.udpEndpoint == remote_endpoint)
			return &client;
	return nullptr;
}

void evalClientUdpCommand(stringstream& stream, boost::asio::ip::udp::endpoint remote_endpoint) {
	int clientId;
	stream >> clientId;
	for (auto& client: clients) {
		if (client.id == clientId)
		{
			client.udpEndpoint = remote_endpoint;
			client.lastContactTime = timeMillis();
			client.queueState = FIFO_LOW_WATERMARK == 0 ? ACTIVE : FILLING;
			break;
		}
	}
	cerr << "Client registered: " << boost::lexical_cast<std::string>(remote_endpoint) <<
	 " as #" << clientId << endl;
}

void evalUploadUdpCommand(stringstream& stream, Client* client, size_t bytes) { 
	if (client == nullptr)
	{
		cerr << "Client is not registered!";
		return;
	}
	cerr << "UPLOAD " << bytes << endl;
	int packetId;
	stream >> packetId;
	client->lastPacket = packetId;
	stream.get();
	for (int i=stream.tellg(); i<bytes-1; i+=2) {
		if (client->queue.size() == FIFO_SIZE) 
		{
			cerr << "Client " << client->udpEndpoint << " has its queue filled!" << endl;
		}
		else {
			int16_t v = 0;
			char c1 = receivedUdpBuffer[i];
			char c2 = receivedUdpBuffer[i+1];
			//cerr << (int)c1 << " " << (int)c2 << endl;
			v = c1 + c2 * 256;
			client->queue.push_back(v);
		}
	}
	updateMinMaxFifo(*client);
	//cerr << "FIFOSIZE" << clientId << " " << clients[clientId].queue.size() << " " << bytes << endl;
	stringstream response;
	response << "ACK " << packetId << " " << (FIFO_SIZE - client->queue.size() * 2) << '\n';
	shared_ptr<string> responseStr(new string(response.str()));
	cerr << "POTWIERDZAM " << packetId << endl;
	socketDatagram.async_send_to(
		boost::asio::buffer(*responseStr, responseStr->length()),
		client->udpEndpoint,
		[&] (const e_code& error, std::size_t bytes_transferred) {
			cerr << "POTWIERDZONE." << endl;
	    	handleError(error, "upload:sendAcknowledgement");		    
		});
}

void evalKeepaliveUdpCommand(stringstream& stream, Client* client, size_t bytes) {

}

void evalRetransmitUdpCommand(stringstream& stream, Client* client, size_t bytes) {

}

void udpReceiveNext() {
	socketDatagram.async_receive_from(
		boost::asio::buffer(receivedUdpBuffer, 10000),
		udpReceivedEndpoint,
		[] (const e_code& error,
			size_t bytes_transferred) {
			if (bytes_transferred > 0) {
				if (handleError(error, "onUdpReceived"))
					return;
				Client* client = recognizeClient(udpReceivedEndpoint);
				if (client)
					client->lastContactTime = timeMillis();
				stringstream stream(receivedUdpBuffer);
				string command;
				stream >> command;
				if (command == "CLIENT")
					evalClientUdpCommand(stream, udpReceivedEndpoint);
				else if (command == "UPLOAD")
					evalUploadUdpCommand(stream, client, bytes_transferred);
				else if (command == "KEEPALIVE")
					evalKeepaliveUdpCommand(stream, client, bytes_transferred);
				else if (command == "RETRANSMIT")
					evalRetransmitUdpCommand(stream, client, bytes_transferred);
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
	for (int i=0; i<clients.size(); i++) {
		Client& client = clients[i];
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

	shared_ptr<string> state(new string(strstate.str()));
	for (auto& client: clients) {
		if (client.queueState != ERROR)
			boost::asio::async_write(*client.socket, 
				boost::asio::buffer(*state, state->length()), 
				[&] (const e_code& error, std::size_t bytes_transferred) {
	    			handleError(error, "sendPeriodicState");
		   		});
	}
	periodicSender.expires_at(periodicSender.expires_at() + boost::posix_time::seconds(1));
	periodicSender.async_wait(&sendPeriodicState);
}

size_t mix() {
	vector<mixer_input> inputs;
	vector<Client*> inputToClient;
	for (auto& client: clients) {
		if (client.queueState == ERROR)
			continue;
		mixer_input in;
		in.data = (void*)&(*client.queue.begin());
		in.len = client.queueState == FILLING ? 0 : client.queue.size() * 2;
		in.consumed = 0;
		inputs.push_back(in);
		inputToClient.push_back(&client);
	}
	size_t output_size;
	mixer(&inputs[0], inputs.size(), mixedOutputBuffer, &output_size, TX_INTERVAL);
	for (int i=0; i<inputs.size(); i++) {
		Client& client = *inputToClient[i];
		client.queue.erase(client.queue.begin(), client.queue.begin() + inputs[i].consumed / 2);
		updateMinMaxFifo(client);
	}
	return output_size;
}

void updateMinMaxFifo(Client& client) {
	int sz = client.queue.size() * 2;
	if (client.minFifoSecond == 0)
		client.minFifoSecond = sz;
	else
		client.minFifoSecond = min(client.minFifoSecond, sz);
	client.maxFifoSecond = max(client.maxFifoSecond, sz);

	if (client.queueState == FILLING && 
		sz >= FIFO_HIGH_WATERMARK)
		client.queueState = ACTIVE;
	if (client.queueState == ACTIVE && 
		sz <= FIFO_LOW_WATERMARK)
		client.queueState = FILLING;	
}

void setErrorConnectionState(Client& client) {
	client.queueState = ERROR;
	if (client.socket)
		client.socket->close();
	cerr << "Connection error for client." << endl;
}

void transmitData(const e_code&) {
	size_t size = mix();
	transmitter.expires_at(transmitter.expires_at() + boost::posix_time::milliseconds(TX_INTERVAL));
	transmitter.async_wait(&transmitData);
	for (auto& client: clients) {
		if (client.queueState == ERROR || client.queueState == TCP_ONLY)	
			continue;
		stringstream header;
		header << "DATA " << lastData << " " << client.lastPacket << " " << 
			(FIFO_SIZE - client.queue.size() * 2) << '\n';
		header.write((char*)mixedOutputBuffer, size);
		shared_ptr<string> toSend(new string(header.str()));
		socketDatagram.async_send_to(
			boost::asio::buffer(*toSend, toSend->length()),
			client.udpEndpoint,
			[&] (const e_code& error, std::size_t bytes_transferred) {
	    		handleError(error, "transmitData", &client);
			});
	}
	lastData++;
}

bool handleError(const e_code& error, const string& caller, Client* client) {
	if (!error)
		return false;
	if (client != nullptr)
		setErrorConnectionState(*client);
	cerr << "Error: " << error << " in " << caller << endl;
	cerr << error.message() << endl;
	return true;
}
