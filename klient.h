#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <sstream>
#include <vector>
#include <algorithm>

using namespace std;
typedef boost::system::error_code e_code;

extern int PORT;// numer portu, z którego korzysta serwer do komunikacji (zarówno TCP, jak i UDP), domyślnie 10000 + (numer_albumu % 10000); ustawiany parametrem -p serwera, opcjonalnie też klient (patrz opis)
extern int FIFO_SIZE; // rozmiar w bajtach kolejki FIFO, którą serwer utrzymuje dla każdego z klientów; ustawiany parametrem -F serwera, domyślnie 10560
extern int FIFO_LOW_WATERMARK;// opis w treści; ustawiany parametrem -L serwera, domyślnie 0
extern int FIFO_HIGH_WATERMARK;// opis w treści; ustawiany parametrem -H serwera, domyślnie równy FIFO_SIZE
extern int BUF_LEN; // rozmiar (w datagramach) bufora pakietów wychodzących, ustawiany parametrem -X serwera, domyślnie 10 
extern int RETRANSMIT_LIMIT; // opis w treści; ustawiany parametrem -X klienta, domyślnie 10
extern int TX_INTERVAL;
extern boost::asio::io_service ioservice;
extern boost::asio::ip::tcp::endpoint endpoint;
extern boost::asio::ip::udp::endpoint endpoint_udp;
extern boost::asio::ip::udp::socket sock_dgram;
extern boost::asio::ip::tcp::socket sock_stream;
extern boost::asio::streambuf tcpbuffer;
extern boost::asio::streambuf inbuffer;
extern char udpBuffer[];
extern boost::asio::posix::stream_descriptor input_;
extern vector<int16_t> dataToSend;
extern string currentSentData;
extern unsigned long window;
int main(int, char**);
void start();
void abort();
void transmitNextPack();
void onSigint(const e_code&, int);
void readNextReportLine();
void readNextStdinLine();
void handshakeUdp(int);
void readNextDatagram();

extern int packId;
extern int lastReceivedData;