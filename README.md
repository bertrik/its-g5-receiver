# its-g5-receiver
Simple ITS-G5 receiver for ITS-G5 802.11p frames

This project consists of two parts:
* A 802.11p packet sniffer receiving the C-ITS G5 messages, implemented using an esp32-c5
* A bridge that receives the sniffed packets and forwards them to MQTT, implemented using an esp32-c3
