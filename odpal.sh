sox sample.mp3 -t raw -r 44100 -b 16 -c 2 -e signed-integer - | ./klient | play -t raw -r 44100 -b 16 -c 2 -e signed-integer -
