#include "serwer.h"
#include <algorithm>
#include <cstdio>
using namespace std;

const int TX_LEN = 176;

void mixer(struct mixer_input* inputs, 
  size_t n, 
  void* output_buf,
  size_t* output_size,
  unsigned long tx_interval_ms) 
  {
	unsigned long len = tx_interval_ms * TX_LEN / 2;
	int pos = 0;
	while (pos < len) {
		int16_t val = 0;
		for (int i=0; i<n; i++) {
			if (pos >= inputs[i].len)
				continue;
			val = min((((int16_t*)inputs[i].data)[pos]) + val, INT16_MAX);
			inputs[i].consumed += 2;
		}
		((int16_t*)output_buf)[pos] = val;
		pos++;
	}
	*output_size = tx_interval_ms * TX_LEN;
}
