sox sample.mp3 -t raw -r 16000 -b 16 -c 1 -e signed-integer - | ./klient | play -t raw -r 16000 -b 16 -c 1 -e signed-integer -
