#include "klient.h"

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
int packId = 0;
unsigned long window = 0;
int lastReceivedData = 0;
char udpBuffer[10001];

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
string currentSentData;
vector<int16_t> dataToSend;

int main(int argc, char** argv) { 
	start();
	ioservice.run();
}

void start() {
	endpoint.address(boost::asio::ip::address_v4::from_string("127.0.0.1"));
	endpoint.port(PORT);
	endpoint_udp = boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 15555);
	endpoint_udp_server = boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 14582);
	sock_stream.connect(endpoint);
	sock_dgram.open(endpoint_udp.protocol());
	sock_dgram.bind(endpoint_udp);
	cerr << "Client Started!" << endl;
	boost::asio::async_read_until(sock_stream, tcpbuffer, '\n', 
		[&] (const e_code& errorcode, size_t bytes_received) {
			boost::asio::streambuf::const_buffers_type bufs = tcpbuffer.data();
			std::string str(boost::asio::buffers_begin(bufs), boost::asio::buffers_begin(bufs) + bytes_received);
			tcpbuffer.consume(bytes_received);
			stringstream data(str);
			string cmd;
			int clientId; 
			data >> cmd >> clientId;
			cerr << cmd << " " << clientId << endl;
			if (cmd != "CLIENT") {
				cerr << "Error! Unknown Server Type!" << endl;
			}
			input_.assign( STDIN_FILENO );
			readNextReportLine();
			readNextStdinLine();
			cerr << "Registered as client " << clientId << endl;
			handshakeUdp(clientId);
			readNextDatagram();
		});
}

void readNextReportLine() {
	boost::asio::async_read_until(sock_stream, tcpbuffer, '\n', 
		[&] (const e_code& errorcode, size_t bytes_received) {
			if (errorcode)
				cerr << "ERROR!";
			boost::asio::streambuf::const_buffers_type bufs = tcpbuffer.data();
			std::string str(boost::asio::buffers_begin(bufs), boost::asio::buffers_begin(bufs) + bytes_received);
			tcpbuffer.consume(bytes_received);
			cerr << str;
			readNextReportLine();
		});
}

char inbuf[2];
void readNextStdinLine() {
  	boost::asio::async_read(input_, boost::asio::buffer(inbuf, 2),
    	[&] (const e_code& errorcode, size_t bytes_received) {
    		if (errorcode)
    			cerr << "CERROR" << endl;
    		dataToSend.push_back(*((int32_t*)inbuf));
    		readNextStdinLine();
    	});
}


void abort() {

}

void handshakeUdp(int clientId) {
	stringstream data;
	data << "CLIENT " << clientId << '\n';
	currentSentData = data.str();
	sock_dgram.async_send_to(
		boost::asio::buffer(currentSentData, currentSentData.length()),
		endpoint_udp_server,
		[&] (const e_code& error, std::size_t bytes_transferred) {
		    transmitNextPack();
		});
}

void transmitNextPack() {
	stringstream data;
	data << "UPLOAD " << packId << "\n";
	int dataSent = min(dataToSend.size(), window);
	if (dataSent > 0)
		cerr << "WYSYLAM " << dataSent << endl;
	data.write((char*)(&(*dataToSend.begin())), min(dataToSend.size()*2, window));
	currentSentData = data.str();
	if (dataSent > 0)
		cerr << data.str();
	sock_dgram.async_send_to(
		boost::asio::buffer(currentSentData, currentSentData.length()),
		endpoint_udp_server,
		[&] (const e_code& error, std::size_t bytes_transferred) {
		    dataToSend.erase(dataToSend.begin(), dataToSend.begin() + dataSent);
		});
}

void handleAck(int ackId) {
	//cerr << "ACK" << ackId<<endl;
	packId++;
	transmitNextPack();
}

void handleData(int id, int ack, int win, stringstream& data, size_t bytes_transferred) {
	window = win;
	lastReceivedData = id;
	bool sent = false;
	for (int i=data.tellg(); i<bytes_transferred-1; i++) {
		if (!sent) cout << "Incoming:";
		sent = true;
		cout << (char)data.get();
	}
	if (sent)
		cout << endl;
}

void readNextDatagram() {
	sock_dgram.async_receive_from(
		boost::asio::buffer(udpBuffer, 10000),
		endpoint_udp_server,
		[&] (const e_code& error, std::size_t bytes_transferred) {
			stringstream data(udpBuffer);
			string command;
			data >> command;
			if (command == "DATA") {
				int id, ack, win;
				data >> id >> ack >> win;
				handleData(id, ack, win, data, bytes_transferred);
			} else if (command == "ACK") {
				int ackId;
				data >> ackId;
				handleAck(ackId);
			}
			readNextDatagram();
		});
}