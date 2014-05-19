#ifndef SERWER_H
#define SERWER_H

#include <iostream>
#include <queue>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <chrono>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

using namespace std;
typedef boost::system::error_code e_code;
typedef boost::asio::ip::udp::endpoint udp_endpoint;
namespace po = boost::program_options;

enum QueueState {
  FILLING,
  ACTIVE,
  ERROR, // polaczenie jest klopotliwe
  TCP_ONLY // powiazanie TCP jeszcze nie nastapilo
};

struct mixer_input {
  void* data;       // Wskaznik na dane w FIFO
  size_t len;       // Liczba dostepnych bajtow
  size_t consumed;  // Wartosc ustawiana przez mikser, wskazujaca, ile
                    // bajtow nalezy usunac z FIFO.
};

struct Client {
  Client();
  int id; // identyfikator klienta nadany przez serwer
  QueueState queueState;
  vector<int16_t> queue; // kolejka FIFO wartosci dla miksera
  boost::asio::ip::tcp::socket* socket; // socket TCP do wysylania raportow
  udp_endpoint udpEndpoint; // endpoint UDP do wysylania danych
  int lastPacket; // numer ostatniego pakietu potwierdzonego przez serwer
  int minFifoSecond; // minimalna ilosc danych FIFO w ostatniej sekundzie
  int maxFifoSecond; // j.w.
  unsigned long lastContactTime; // kiedy ostatnio klient kontaktowal sie z serwerem
};

void mixer(
  struct mixer_input* inputs,
  size_t n,
  void* output_buf,
  size_t* output_size,
  unsigned long tx_interval_ms);

void start(int port); // inicjuje serwer na podanym porcie i rozpoczyna nasluch
void deinit(); // konczy prace programu
void acceptNext(); // akceptuje kolejnego klienta TCP 
void onAccept(const e_code&); // zdarzenie przy kolejnym gotowym kliencie
void sendPeriodicState(const e_code&); // wysyla okresowa informacje o stanie kolejek (TCP)
void onSigint(const e_code&, int); // operacja przed zakonczeniem programu
void udpReceiveNext(); // polecenie pobierajace kolejny datagram
void onUdpReceived(const e_code&, std::size_t); // handler datagramu
void evalClientUdpCommand(stringstream&, udp_endpoint); // procesor obslugi polecenia CLIENT
void evalUploadUdpCommand(stringstream&, int, size_t); // procesor obslugi polecenia UPLOAD
void evalRetransmitUdpCommand(stringstream&, int, size_t); // procesor obslugi polecenia UPLOAD
void evalKeepaliveUdpCommand(stringstream&, int, size_t); // procesor obslugi polecenia KEEPALIVE
void evalRetransmitUdpCommand(stringstream&, int, size_t); // procesor obslugi polecenia RETRANSMIT
void transmitData(const e_code&); // transmituje dane do wszystkich klientów
void updateMinMaxFifo(Client&); // aktualizuje kolejkę FIFO
void setErrorConnectionState(Client&); // ustawia stan polaczenia w tryb klopotliwy
void watchdogElapsed(const e_code& error = e_code()); // sprawdza, czy klienci są w łączności
unsigned long timeMillis();
void onUdpReceived(const e_code& error,
  size_t bytes_transferred,
  shared_ptr<vector<char>> udpBuffer,
  udp_endpoint remote_endpoint);
bool handleError(const e_code&, const string&, Client* client = nullptr); // zajmuje się obsługą błędu
size_t mix(); // generuje bufor do wysłania klientom

#endif
