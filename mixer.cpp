#include "serwer.h"
#include <algorithm>
#include <cstdio>
using namespace std;

const float TX_LEN = 176.4;

void mixer(struct mixer_input* inputs, 
  size_t n, 
  void* output_buf,
  size_t* output_size,
  unsigned long tx_interval_ms) 
  {
	unsigned long len = tx_interval_ms * (TX_LEN / 2);
	int pos = 0;
	//if (n > 0)
	//cerr << "len " << inputs[0].len << endl;
	bool usingzeros =false;
	while (pos < len) {
		int16_t val = 0;
		for (int i=0; i<n; i++) {
			if (pos >= inputs[i].len / 2) {
				usingzeros = true;
				continue;
			}
		//	val = min((((int16_t*)inputs[i].data)[pos]) + val, INT16_MAX);
			val = ((int16_t*)inputs[i].data)[pos];
			inputs[i].consumed += 2;
		}
		((int16_t*)output_buf)[pos] = val;
		pos++;
	}
	if (usingzeros)
		cerr << "usingzeros!!" << inputs[0].len << endl;
	//if (n > 0)
	//	cerr << "len " << inputs[0].consumed << endl;
	*output_size = pos * 2;
}
