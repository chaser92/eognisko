#include <iostream>
#include <boost/asio/ip/tcp.hpp>
#include <queue>
#include <vector>

enum QueueState {
	FILLING,
	ACTIVE
};

struct mixer_input {
  void* data;       // Wskaźnik na dane w FIFO
  size_t len;       // Liczba dostępnych bajtów
  size_t consumed;  // Wartość ustawiana przez mikser, wskazująca, ile bajtów należy
                    // usunąć z FIFO.
};

void mixer(
  struct mixer_input* inputs, 
  size_t n, 
  void* output_buf,
  size_t* output_size,
  unsigned long tx_interval_ms);

void start(int port);
void acceptNext();
void onAccept(const boost::system::error_code&);
void deinit();

boost::asio::io_service ioservice;
boost::asio::ip::tcp::acceptor acceptor(ioservice);
boost::asio::ip::tcp::endpoint endpoint;

struct Client {
	Client(boost::asio::io_service&);
	QueueState queueState;
	std::queue<int> queue;
	boost::asio::ip::tcp::socket socket;
};

std::vector<Client> clients;

int PORT = 14582;// numer portu, z którego korzysta serwer do komunikacji (zarówno TCP, jak i UDP), domyślnie 10000 + (numer_albumu % 10000); ustawiany parametrem -p serwera, opcjonalnie też klient (patrz opis)
int FIFO_SIZE = 10560; // rozmiar w bajtach kolejki FIFO, którą serwer utrzymuje dla każdego z klientów; ustawiany parametrem -F serwera, domyślnie 10560
int FIFO_LOW_WATERMARK = 0;// opis w treści; ustawiany parametrem -L serwera, domyślnie 0
int FIFO_HIGH_WATERMARK = FIFO_SIZE;// opis w treści; ustawiany parametrem -H serwera, domyślnie równy FIFO_SIZE
int BUF_LEN = 10; // rozmiar (w datagramach) bufora pakietów wychodzących, ustawiany parametrem -X serwera, domyślnie 10 
int RETRANSMIT_LIMIT = 10; // opis w treści; ustawiany parametrem -X klienta, domyślnie 10
int TX_INTERVAL = 5; // czas (w milisekundach) pomiędzy kolejnymi wywołaniami miksera, ustawiany parametrem -i serwera; domyślnie: 5ms