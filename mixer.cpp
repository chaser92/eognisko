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
	unsigned long pos = 0;
	//if (n > 0)
	//cerr << "len " << inputs[0].len << endl;
	while (pos < len) {
		int16_t val = 0;
		for (unsigned long i=0; i<n; i++) {
			if (pos >= inputs[i].len / 2) {
				continue;
			}
			int32_t tmp_val = (int32_t)(((int16_t*)inputs[i].data)[pos]) + val;
			if (tmp_val > INT16_MAX)
				val = INT16_MAX;
			else if (tmp_val < INT16_MIN)
				val = INT16_MIN;
			else
				val = tmp_val;
			inputs[i].consumed += 2;
		}
		((int16_t*)output_buf)[pos] = val;
		pos++;
	}

	*output_size = pos * 2;
}
