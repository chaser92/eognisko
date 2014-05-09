#include <iostream>
#include <queue>
#include <vector>
#include <string>
#include <sstream>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/asio/ip/tcp.hpp>


typedef boost::system::error_code e_code;
using namespace std;

enum QueueState {
  FILLING,
  ACTIVE,
  ERROR,
  UNINITIALIZED
};

struct mixer_input {
  void* data;       // Wskaznik na dane w FIFO
  size_t len;       // Liczba dostepnych bajtow
  size_t consumed;  // Wartosc ustawiana przez mikser, wskazująca, ile
                    // bajtow nalezy usunac z FIFO.
};

void mixer(
  struct mixer_input* inputs,
  size_t n,
  void* output_buf,
  size_t* output_size,
  unsigned long tx_interval_ms);

void start(int port);
void acceptNext();
void onAccept(const e_code&);
void deinit();
void sendOutputDatagram();
void sendPeriodicState(const e_code&);
void acknowledge();
void onSigint(const e_code&, int);
void udpReceiveNext(int clientId);
void onUdpReceived(const e_code&, std::size_t, int);
void evalDataUdpCommand(stringstream&, int);
void evalClientUdpCommand(stringstream&, int);
void transmitData(const e_code&);

boost::asio::io_service ioservice;
boost::asio::ip::tcp::acceptor acceptor(ioservice);
boost::asio::deadline_timer periodicSender(ioservice, boost::posix_time::seconds(1));
boost::asio::deadline_timer transmitter(ioservice, boost::posix_time::milliseconds(5));
boost::asio::signal_set signals(ioservice, SIGINT, SIGTERM);
boost::asio::ip::tcp::endpoint endpoint;
boost::asio::ip::udp::endpoint endpoint_udp;
boost::asio::ip::udp::socket sock_dgram(ioservice);

struct Client {
  Client();
  QueueState queueState;
  std::queue<int> queue;
  boost::asio::ip::tcp::socket* socket;
  boost::asio::ip::udp::endpoint udpEndpoint;
  bool udpRegistered;
};

std::vector<Client> clients;
char udpBuffer[10000];
Client toAccept;

int lastUpload = 0;
int lastClient = 0;
int PORT = 14582;// numer portu, z którego korzysta serwer do komunikacji (zarówno TCP, jak i UDP), domyślnie 10000 + (numer_albumu % 10000); ustawiany parametrem -p serwera, opcjonalnie też klient (patrz opis)
int FIFO_SIZE = 10560; // rozmiar w bajtach kolejki FIFO, którą serwer utrzymuje dla każdego z klientów; ustawiany parametrem -F serwera, domyślnie 10560
int FIFO_LOW_WATERMARK = 0;// opis w treści; ustawiany parametrem -L serwera, domyślnie 0
int FIFO_HIGH_WATERMARK = FIFO_SIZE;// opis w treści; ustawiany parametrem -H serwera, domyślnie równy FIFO_SIZE
int BUF_LEN = 10; // rozmiar (w datagramach) bufora pakietów wychodzących, ustawiany parametrem -X serwera, domyślnie 10 
int RETRANSMIT_LIMIT = 10; // opis w treści; ustawiany parametrem -X klienta, domyślnie 10
int TX_INTERVAL = 5; // czas (w milisekundach) pomiędzy kolejnymi wywołaniami miksera, ustawiany parametrem -i serwera; domyślnie: 5ms