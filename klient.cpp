#include "klient.h"

// czy maja byc pokazywane informacje debugera
const bool DEBUG = false;

// rozmiar maksymalny wychodzącego datagram UDP
unsigned long UDP_MAX_SIZE = 2048;

// host serwera
string SERVER_NAME = "127.0.0.1";

// numer portu dla serwera
int PORT = 14582;


// numer nowej paczki do nadania
int packId = 0;

// ostatnio podany rozmiar okna
unsigned long window = 0;

// bufor na przychodzące datagramy UDP
char udpBuffer[1000001];

// usługa ASIO
boost::asio::io_service ioservice;

// endpointy TCP i nadawania UDP
boost::asio::ip::tcp::endpoint endpoint;
boost::asio::ip::udp::endpoint endpoint_udp;

// endpoint odbierania UDP od serwera
boost::asio::ip::udp::endpoint endpoint_udp_server;

// socket do wymiany danych UDP
boost::asio::ip::udp::socket socketDatagram(ioservice);

// socket do odbioru raportów
boost::asio::ip::tcp::socket socketStream(ioservice);

boost::asio::streambuf tcpbuffer;
boost::asio::streambuf stdinbuffer;
boost::asio::streambuf udpbuffer;

// do asynchronicznego IO z standardowych deskryptorów
boost::asio::posix::stream_descriptor input_(ioservice);
boost::asio::posix::stream_descriptor output_(ioservice);


string currentSentData;

// dane pobrane ze standardowego wejścia, które nie zostały jeszcze nadane
queue<char> dataToSend;

// dane gotowe do wypisania na standardowe wyjście
queue<string> partsToWrite;

// timer do wysyłania komunikatów KEEPALIVE
boost::asio::deadline_timer keepaliveTimer(ioservice, boost::posix_time::milliseconds(10));

// wypisuje kolejną partię na standardowe wyjście
boost::asio::deadline_timer nextpackTimer(ioservice, boost::posix_time::milliseconds(5));

// transmituje kolejny datagram UPLOAD
boost::asio::deadline_timer nextTransmitTimer(ioservice, boost::posix_time::milliseconds(3));

// timer do odczytu kolejnej porcji z STDIN
boost::asio::deadline_timer nextStdinTimer(ioservice, boost::posix_time::milliseconds(3));

// timer do odczytu kolejnej porcji z STDIN
boost::asio::deadline_timer reconnectTimer(ioservice, boost::posix_time::seconds(1));

// sprawdza, czy klienci są w kontakcie z serwerem
boost::asio::deadline_timer connectivityWatchdog(ioservice, boost::posix_time::milliseconds(50));
string keepaliveText = "KEEPALIVE\n";
bool connectionOk = false;
unsigned long serverLastActive = 0;
unsigned long serverLastAck = 0;

int main(int argc, char** argv) { 
	po::options_description desc("Allowed options");
	desc.add_options()
		("help,h", "produce help message")
		("server,s", po::value<string>(), "set server name")
		("port,p", po::value<int>(), "set server port");

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

	if (vm.count("server"))
		SERVER_NAME = vm["server"].as<string>();
	if (vm.count("port"))
		PORT = vm["port"].as<int>();
	input_.assign( STDIN_FILENO );
	output_.assign( STDOUT_FILENO );
	ios::sync_with_stdio(false);
 	serverLastActive = timeMillis();
	serverLastAck = timeMillis();
    watchdogElapsed();
	reconnect();
	ioservice.run();	
}

void reconnect(const e_code&) {
	cerr << "Reconnecting..." << endl;
	try {
		start();
	}
	catch (exception const& exc)
	{
		cerr << exc.what() << endl;
		cerr << "Retrying...";
 		reconnectTimer.expires_at(reconnectTimer.expires_at() + boost::posix_time::seconds(1));
 		reconnectTimer.async_wait(&reconnect);  		
	}
}

unsigned long timeMillis() {
    return std::chrono::system_clock::now().time_since_epoch() / 
        std::chrono::milliseconds(1);
}

void watchdogElapsed(const e_code&) {
    connectivityWatchdog.expires_at(connectivityWatchdog.expires_at() + boost::posix_time::milliseconds(50));
    connectivityWatchdog.async_wait(&watchdogElapsed);    
    if (!connectionOk)
        return;
    unsigned long now = timeMillis();
    if (serverLastAck < now - 500) 
       retransmit();
  }

void retransmit() {

}

bool handleError(const e_code& error, string caller) {
	if (!error)
		return false;
	cerr << "Error: " << error << " in " << caller << endl;
	cerr << error.message() << endl;
	abortApp();
	reconnect();
	return true;
}

void start() {
	cerr << "start" << endl;
	boost::asio::ip::tcp::resolver resolver(ioservice);

	boost::asio::ip::tcp::resolver::iterator endpoint_iterator = 
		resolver.resolve(boost::asio::ip::tcp::resolver::query(SERVER_NAME, to_string(PORT)));
    boost::asio::ip::tcp::resolver::iterator endIt;
    boost::system::error_code error = boost::asio::error::host_not_found;

    while (error && endpoint_iterator != endIt)
    {
    	socketStream.close();
		socketStream.connect(*endpoint_iterator, error);
		if (!error)
			endpoint = *endpoint_iterator;
		endpoint_iterator++;
    }
    if (error)
    	throw boost::system::system_error(error);
    cerr << "idk" << endl;
	endpoint_udp_server.address(endpoint.address());
	endpoint_udp_server.port(endpoint.port());
	socketDatagram.close();
	socketDatagram.open(endpoint_udp_server.protocol());
	cerr << "Client Started!" << endl;
	serverLastActive = timeMillis();
	serverLastAck = timeMillis();
	boost::asio::async_read_until(socketStream, tcpbuffer, '\n', 
		[&] (const e_code& error, size_t bytes_received) {
			if (handleError(error, "start:tcp_read_greeting"))
				return;
			boost::asio::streambuf::const_buffers_type bufs = tcpbuffer.data();
			std::string str(boost::asio::buffers_begin(bufs), boost::asio::buffers_begin(bufs) + bytes_received);
			tcpbuffer.consume(bytes_received);
			stringstream data(str);
			string cmd;
			int clientId; 
			data >> cmd >> clientId;
			if (cmd != "CLIENT") {
				cerr << "Error! Unknown Server Type!" << endl;
			}
			serverLastActive = timeMillis();
			serverLastAck = timeMillis();
			connectionOk = true;
			readNextReportLine();
			readNextStdinPart();
			cerr << "Registered as client " << clientId << endl;
			handshakeUdp(clientId);
			nextKeepalive();
			readNextDatagram();
			writeNextPack();
		});
}

void readNextReportLine() {
	if (connectionOk)
	boost::asio::async_read_until(socketStream, tcpbuffer, '\n', 
		[&] (const e_code& error, size_t bytes_received) {
			if (handleError(error, "readNextReportLine"))
				return;
			boost::asio::streambuf::const_buffers_type bufs = tcpbuffer.data();
			std::string str(boost::asio::buffers_begin(bufs), boost::asio::buffers_begin(bufs) + bytes_received);
			tcpbuffer.consume(bytes_received);
			cerr << str << endl;
			readNextReportLine();
		});
}

int read_from_stdin_total = 0;
char inbuf[2048];
void readNextStdinPart(const e_code&) {
	if (connectionOk) {
  	boost::asio::async_read(input_, boost::asio::buffer(inbuf, 2048),
    	[&] (const e_code& error, size_t bytes_received) {
    		read_from_stdin_total += bytes_received;
			if (error)
				return;
			for (size_t i=0; i<bytes_received; i++) {
				dataToSend.push(inbuf[i]);
			}
			readNextStdinPart();
    	});
  	}
}

void abortApp() {
	cerr << "Aborting..." << endl;
	connectionOk = false;
}

void handshakeUdp(int clientId) {
	stringstream data;
	data << "CLIENT " << clientId << '\n';
	currentSentData = data.str();
	socketDatagram.async_send_to(
		boost::asio::buffer(currentSentData, currentSentData.length()),
		endpoint_udp_server,
		[&] (const e_code& error, std::size_t bytes_transferred) {
			if (handleError(error, "handshakeUdp"))
				return;	
		});
}

int totalTransferred = 0;

void transmitNextPack(const e_code& error) {
	if (handleError(error, "transmitNextPack (timer)"))
		return;	
	if (!connectionOk)
		return;
	stringstream data;
	data << "UPLOAD " << packId << "\n";
	int dataSent = min(UDP_MAX_SIZE, min((unsigned long)dataToSend.size(), window));
	if (DEBUG)
		cerr << "WYSYLAM DANE " << packId << endl;
	if (DEBUG)
		cerr << "Okno to " << window << ", a danych wyslemy " << dataSent << endl;
	for (int i=0; i<dataSent; i++) {
		data << dataToSend.front();
		dataToSend.pop();
	}
	totalTransferred += dataSent;
	currentSentData = data.str();
	socketDatagram.async_send_to(
		boost::asio::buffer(currentSentData, currentSentData.length()),
		endpoint_udp_server,
		[&] (const e_code& error, std::size_t bytes_transferred) {
			if (handleError(error, "transmitNextPack"))
				return;
		});
}


void handleAck(int ackId, unsigned long win) {
	if (DEBUG)
		cerr << "ACK " << ackId << " " << packId <<endl;
	window = win;
	if (ackId >= packId) {
		packId++;
		if (win > 0)
			transmitNextPack(e_code());
		else { 
			nextTransmitTimer.expires_at(nextTransmitTimer.expires_at() + boost::posix_time::milliseconds(3));
			nextTransmitTimer.async_wait(&transmitNextPack);
		}
	}
}

void handleData(int id, int ack, int win, stringstream& data, size_t bytes_transferred) {
	if (DEBUG)
		cerr << "DATA " << id << ack << endl;
	window = win;
	handleAck(ack, win);
	
	string dataToPrint(udpBuffer + (int)data.tellg() + 1, 
		bytes_transferred - (int)data.tellg() + 1);
	partsToWrite.push(dataToPrint);
}

void writeNextPack(const e_code&) {
	if (partsToWrite.size() > 0)
	{
		boost::asio::async_write(output_, 
			boost::asio::buffer(partsToWrite.front(), partsToWrite.front().length()),
			[&] (const e_code&, size_t bytes_transferred) {
				partsToWrite.pop();
				writeNextPack();
			});	
	}
	else {
		nextpackTimer.expires_at(nextpackTimer.expires_at() + boost::posix_time::milliseconds(2));
		nextpackTimer.async_wait(&writeNextPack);
	}
}

void readNextDatagram() {
	if (connectionOk)
	socketDatagram.async_receive_from(
		boost::asio::buffer(udpBuffer, 1000000),
		endpoint_udp_server,
		[&] (const e_code& error, std::size_t bytes_transferred) {
			if (handleError(error, "readNextDatagram"))
				return;
			serverLastActive = time(0);
			stringstream data(udpBuffer);
			string command;
			data >> command;
			if (command == "DATA") {
				int id, ack, win;
				data >> id >> ack >> win;
				handleData(id, ack, win, data, bytes_transferred);
			} else if (command == "ACK") {
				int ackId, win;
				data >> ackId >> win;
				handleAck(ackId, win);
			}
			else {
				cerr << "Unsupported command: " << command << endl;
			}
			readNextDatagram();		
		});
}

void nextKeepalive(const e_code& error) {
	if (handleError(error, "nextKeepalive (timer)"))
		return;
	if (error) {
		cerr << error.message() << endl;
		connectionOk = false;
		return;	
	}
	if (connectionOk)
	socketDatagram.async_send_to(
	boost::asio::buffer(keepaliveText, keepaliveText.length()),
		endpoint_udp_server,
		[&] (const e_code& error, std::size_t bytes_transferred) {
		if (handleError(error, "nextKeepalive"))
			return;
		});
	keepaliveTimer.expires_at(keepaliveTimer.expires_at() + boost::posix_time::milliseconds(10));
	keepaliveTimer.async_wait(&nextKeepalive);
}