#include "serwer.h"	

Client::Client(boost::asio::io_service& io_service):
	queueState(FILLING),
	socket(io_service)
{

}

int main(int argc, char** argv) {
	start(PORT);
}

void start(int port) {
	endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port);
	acceptor.open(endpoint.protocol());
	acceptor.bind(endpoint);
	acceptor.listen();
	acceptNext();
}

void acceptNext() {
	//Client client(ioservice);
	clients.push_back(Client(ioservice));
	acceptor.async_accept(client.socket, onAccept);
}

void onAccept(const boost::system::error_code& error) {
	if (!error)
		acceptNext();
}