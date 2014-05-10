#ifndef SERWER_H
#define SERWER_H

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
void evalClientUdpCommand(stringstream&, int, size_t);
void evalUploadUdpCommand(stringstream&, int, size_t);
void evalRetransmitUdpCommand(stringstream&, int, size_t);
void evalAckUdpCommand(stringstream&, int, size_t);
void evalKeepaliveUdpCommand(stringstream&, int, size_t);
void transmitData(const e_code&);
size_t mix();

extern boost::asio::io_service ioservice;
extern boost::asio::ip::tcp::acceptor acceptor;
extern boost::asio::deadline_timer periodicSender;
extern boost::asio::deadline_timer transmitter;
extern boost::asio::signal_set signals;
extern boost::asio::ip::tcp::endpoint endpoint;
extern boost::asio::ip::udp::endpoint endpoint_udp;
extern boost::asio::ip::udp::socket sock_dgram;

struct Client {
  Client();
  QueueState queueState;
  vector<int16_t> queue;
  boost::asio::ip::tcp::socket* socket;
  boost::asio::ip::udp::endpoint udpEndpoint;
  bool udpRegistered;
  int lastPacket;
  string buf_dgram;
};

extern std::vector<Client> clients;
extern char udpBuffer[10000];
extern Client toAccept;
extern string state;
extern int16_t output_buf[20000]; 
extern int lastUpload;
extern int lastData;
extern int lastClient;
extern int PORT;// numer portu, z którego korzysta serwer do komunikacji (zarówno TCP, jak i UDP), domyślnie 10000 + (numer_albumu % 10000); ustawiany parametrem -p serwera, opcjonalnie też klient (patrz opis)
extern int FIFO_SIZE; // rozmiar w bajtach kolejki FIFO, którą serwer utrzymuje dla każdego z klientów; ustawiany parametrem -F serwera, domyślnie 10560
extern int FIFO_LOW_WATERMARK;// opis w treści; ustawiany parametrem -L serwera, domyślnie 0
extern int FIFO_HIGH_WATERMARK;// opis w treści; ustawiany parametrem -H serwera, domyślnie równy FIFO_SIZE
extern int BUF_LEN; // rozmiar (w datagramach) bufora pakietów wychodzących, ustawiany parametrem -X serwera, domyślnie 10 
extern int RETRANSMIT_LIMIT; // opis w treści; ustawiany parametrem -X klienta, domyślnie 10
extern int TX_INTERVAL; // czas (w milisekundach) pomiędzy kolejnymi wywołaniami miksera, ustawiany parametrem -i serwera; domyślnie: 5ms

#endif