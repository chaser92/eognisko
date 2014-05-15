#include "klient.h"

int lastData = 0;
int lastClient = 0;
int PORT = 14582;// numer portu, z ktorego korzysta serwer do komunikacji (zarówno TCP, jak i UDP), domyślnie 10000 + (numer_albumu % 10000); ustawiany parametrem -p serwera, opcjonalnie też klient (patrz opis)
int BUF_LEN = 10; // rozmiar (w datagramach) bufora pakietów wychodzących, ustawiany parametrem -X serwera, domyślnie 10 
int RETRANSMIT_LIMIT = 10; // opis w treści; ustawiany parametrem -X klienta, domyślnie 10
int TX_INTERVAL = 5; 
int packId = 0;
unsigned long window = 0;
int lastReceivedData = 0;
char udpBuffer[1000001];
unsigned long UDP_MAX_SIZE = 512;
boost::asio::io_service ioservice;
boost::asio::ip::tcp::endpoint endpoint;
boost::asio::ip::udp::endpoint endpoint_udp;
boost::asio::ip::udp::endpoint endpoint_udp_server;
boost::asio::ip::udp::socket sock_dgram(ioservice);
boost::asio::ip::tcp::socket sock_stream(ioservice);
boost::asio::streambuf tcpbuffer;
boost::asio::streambuf stdinbuffer;
boost::asio::streambuf udpbuffer;
boost::asio::posix::stream_descriptor input_(ioservice);
boost::asio::posix::stream_descriptor output_(ioservice);
string currentSentData;
vector<char> dataToSend;
boost::asio::deadline_timer keepaliveTimer(ioservice, boost::posix_time::milliseconds(10));
boost::asio::deadline_timer nextpackTimer(ioservice, boost::posix_time::milliseconds(5));
boost::asio::deadline_timer nextStdinTimer(ioservice, boost::posix_time::milliseconds(3));
string keepaliveText = "KEEPALIVE\n";
bool connectionOk = false;

int main(int argc, char** argv) { 
	start();
	ioservice.run();
}

bool handleError(const e_code& error, string caller) {
	if (!error)
		return false;
	cerr << "Error: " << error << " in " << caller << endl;
	cerr << error.message() << endl;
	connectionOk = false;
	return true;
}

void start() {
	ios::sync_with_stdio(false);
	endpoint.address(boost::asio::ip::address_v4::from_string("127.0.0.1"));
	endpoint.port(PORT);
	endpoint_udp = boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 15555);
	endpoint_udp_server = boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 14582);
	sock_stream.connect(endpoint);
	sock_dgram.open(endpoint_udp.protocol());
	sock_dgram.bind(endpoint_udp);
	cerr << "Client Started!" << endl;
	boost::asio::async_read_until(sock_stream, tcpbuffer, '\n', 
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
			//4cerr << cmd << " " << clientId << endl;
			if (cmd != "CLIENT") {
				cerr << "Error! Unknown Server Type!" << endl;
			}
			connectionOk = true;
			input_.assign( STDIN_FILENO );
			output_.assign( STDOUT_FILENO );
			readNextReportLine();
			readNextStdinLine();
			cerr << "Registered as client " << clientId << endl;
			handshakeUdp(clientId);
			readNextDatagram();
		});
}

void readNextReportLine() {
	if (connectionOk)
	boost::asio::async_read_until(sock_stream, tcpbuffer, '\n', 
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
void readNextStdinLine(const e_code&) {
	if (connectionOk) {
  	boost::asio::async_read(input_, boost::asio::buffer(inbuf, 2048),
    	[&] (const e_code& error, size_t bytes_received) {
    		read_from_stdin_total += bytes_received;
			if (error)
				return;
			for (size_t i=0; i<bytes_received; i++) {
				dataToSend.push_back(inbuf[i]);
			}
			readNextStdinLine();
    		cerr << "Odczytano z wejscia  " << read_from_stdin_total << endl;
    	});
  	}
}

void abort() {
	connectionOk = false;
}

void handshakeUdp(int clientId) {
	stringstream data;
	data << "CLIENT " << clientId << '\n';
	currentSentData = data.str();
	sock_dgram.async_send_to(
		boost::asio::buffer(currentSentData, currentSentData.length()),
		endpoint_udp_server,
		[&] (const e_code& error, std::size_t bytes_transferred) {
			if (handleError(error, "handshakeUdp"))
				return;	
		    transmitNextPack(e_code());
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
	if (window == 0)
		cerr << "Warning: window empty!" << endl;
	int dataSent = min(UDP_MAX_SIZE, min((unsigned long)dataToSend.size(), window));
	cerr << "WYSYLAM DANE " << packId << endl;
	cerr << "Okno to " << window << ", a danych wyslemy " << dataSent << endl;
	data.write(&(*dataToSend.begin()), dataSent);
	totalTransferred += dataSent;
	currentSentData = data.str();
	sock_dgram.async_send_to(
		boost::asio::buffer(currentSentData, currentSentData.length()),
		endpoint_udp_server,
		[&] (const e_code& error, std::size_t bytes_transferred) {
			if (handleError(error, "transmitNextPack"))
				return;
		});
	dataToSend.erase(dataToSend.begin(), dataToSend.begin() + dataSent);
}


void handleAck(int ackId, unsigned long win) {
	cerr << "ACKACKACKACKACKACKACKACAKCAKC"  << endl;
	window = win;
	packId++;
	transmitNextPack(e_code());
}

void handleData(int id, int ack, int win, stringstream& data, size_t bytes_transferred) {
	cerr << "DATA " << id << endl;
	window = win;
	lastReceivedData = id;
	//cerr << "Incoming data (" << bytes_transferred << "):";
	fwrite(udpBuffer + (data.tellg() + 1LL), sizeof(char), 
		bytes_transferred - (data.tellg() + 1LL), stdout);
	//shared_ptr<string> dataToPrint(new string(udpBuffer + (data.tellg() + 1LL), 
	//	bytes_transferred - (data.tellg() + 1LL)));
	//for (int i=0; i<dataToPrint->length(); i++)
//		cerr << (int)((*dataToPrint)[i]) << " ";
	/*boost::asio::async_write(output_, 
		boost::asio::buffer(*dataToPrint, dataToPrint->length()),
		[&] (const e_code&, size_t bytes_transferred) {

		});*/
	//cerr << endl;
}

void readNextDatagram() {
	cerr << "readNextDatagram" << endl;
	if (connectionOk)
	sock_dgram.async_receive_from(
		boost::asio::buffer(udpBuffer, 1000000),
		endpoint_udp_server,
		[&] (const e_code& error, std::size_t bytes_transferred) {
			if (handleError(error, "readNextDatagram"))
				return;
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
	sock_dgram.async_send_to(
	boost::asio::buffer(keepaliveText, keepaliveText.length()),
		endpoint_udp_server,
		[&] (const e_code& error, std::size_t bytes_transferred) {
		if (handleError(error, "nextKeepalive"))
			return;
		});
	keepaliveTimer.expires_at(keepaliveTimer.expires_at() + boost::posix_time::milliseconds(10));
	keepaliveTimer.async_wait(&nextKeepalive);
}