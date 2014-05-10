#include "serwer.h"
#include <algorithm>
#include <cstdio>
using namespace std;

void mixer(struct mixer_input* inputs, 
  size_t n, 
  void* output_buf,
  size_t* output_size,
  unsigned long tx_interval_ms) 
  {
	int pos = 0;
	bool anyoneWritten = true;
	while (anyoneWritten) {
		anyoneWritten = false;
		int16_t val = 0;
		for (int i=0; i<n; i++) {
			if (pos >= inputs[i].len)
				continue;
			anyoneWritten = true;
			val = min((((int16_t*)inputs[i].data)[pos]) + val, INT16_MAX);
			cerr << val << endl;
			inputs[i].consumed++;
		}
		((int16_t*)output_buf)[pos] = val;
		pos++;
	}
	*output_size = pos-1;

}