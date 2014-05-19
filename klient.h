#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/program_options.hpp>
#include <sstream>
#include <vector>
#include <queue>
#include <algorithm>
#include <ctime>

using namespace std;
typedef boost::system::error_code e_code;
namespace po = boost::program_options;

int main(int, char**);
void start();
void abort();
void transmitNextPack(const e_code&);
void onSigint(const e_code&, int);
void readNextReportLine();
void readNextStdinLine(const e_code& errorcode = e_code());
void handshakeUdp(int);
void readNextDatagram();
void nextKeepalive(const e_code&);
bool handleError(const e_code& error, string caller);
void handleAck(int ackId, unsigned long win);
void writeNextPack(const e_code& error = e_code());
